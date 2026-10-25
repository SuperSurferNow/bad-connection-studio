#include "backend/linux/V4L2LoopbackCamera.h"

#include "backend/linux/V4L2Devices.h"

#include <linux/videodev2.h>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace bcs {

std::string V4L2LoopbackCamera::setupHelpHtml()
{
    return R"html(<p><b>v4l2loopback</b> is a Linux kernel module that creates virtual
webcams. Bad Connection Studio does not install kernel modules or run
<code>sudo</code>; run these commands yourself if you want a virtual camera.</p>
<p><b>Fedora 44</b> (the module is packaged in RPM Fusion "free"):</p>
<pre>sudo dnf install https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm
sudo dnf install akmod-v4l2loopback v4l2loopback
sudo akmods --force   # builds the module for the running kernel
sudo modprobe v4l2loopback devices=1 video_nr=10 card_label="Bad Connection Camera" exclusive_caps=1</pre>
<p>With <b>Secure Boot</b> enabled, akmods-built modules must be signed with a
key enrolled via <code>mokutil</code>; see RPM Fusion's "Secure Boot" howto.
Otherwise <code>modprobe</code> reports "Key was rejected by service".</p>
<p>To load it automatically at boot:</p>
<pre>echo v4l2loopback | sudo tee /etc/modules-load.d/v4l2loopback.conf
echo 'options v4l2loopback devices=1 video_nr=10 card_label="Bad Connection Camera" exclusive_caps=1' \
  | sudo tee /etc/modprobe.d/v4l2loopback.conf</pre>
<p><b>Debian/Ubuntu:</b> <code>sudo apt install v4l2loopback-dkms</code>&nbsp;&nbsp;
<b>Arch:</b> <code>sudo pacman -S v4l2loopback-dkms</code>, then the same
<code>modprobe</code> line.</p>
<p><code>exclusive_caps=1</code> is required for Chrome/Chromium-based call
apps to list the device. If the device exists but cannot be opened, your
user needs access to it: local sessions normally get it through systemd-logind
("uaccess"); otherwise add yourself to the <code>video</code> group
(<code>sudo usermod -aG video $USER</code>, then log out and back in).</p>
<p>To remove the virtual camera: <code>sudo modprobe -r v4l2loopback</code>.
Your physical camera is never modified.</p>)html";
}

BackendStatus V4L2LoopbackCamera::probe()
{
    BackendStatus st;
    st.backendId = "v4l2loopback";
    st.displayName = "v4l2loopback (Linux kernel module)";
    st.setupHelp = setupHelpHtml();
    const bool loaded = v4l2::loopbackModuleLoaded();
    for (const auto& d : v4l2::scanDevices()) {
        if (!d.isLoopback)
            continue;
        VirtualDeviceInfo info;
        info.id = d.path;
        info.name = d.card + " (" + d.path + ")";
        if (d.openErrno || !d.writable) {
            info.usable = false;
            info.problem = "no write permission for " + d.path;
        } else if (!d.canOutput && d.canCapture) {
            // With exclusive_caps=1 the node advertises CAPTURE only while a
            // producer is attached.
            info.usable = true;
            info.problem = "already being fed by another program?";
        }
        st.devices.push_back(info);
    }
    int usable = 0;
    for (auto& d : st.devices)
        usable += d.usable;
    st.available = usable > 0;
    if (!loaded)
        st.summary = "v4l2loopback kernel module is not loaded — no virtual camera available";
    else if (st.devices.empty())
        st.summary = "v4l2loopback is loaded but created no devices";
    else if (!usable)
        st.summary = "v4l2loopback device found, but your user cannot write to it";
    else {
        std::string v = v4l2::loopbackModuleVersion();
        st.summary = "v4l2loopback " + (v.empty() ? std::string() : v + " ") + "ready: " +
                     std::to_string(usable) + " virtual camera(s)";
    }
    return st;
}

MediaError V4L2LoopbackCamera::open(const std::string& deviceId, int width, int height, int fps)
{
    close(nullptr);
    m_path = deviceId;
    m_fps = fps > 0 ? fps : 30;
    m_fd = ::open(m_path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) {
        int e = errno;
        ErrorKind k = errorKindFromErrno(e);
        if (k == ErrorKind::DeviceNotFound || k == ErrorKind::DeviceLost)
            k = ErrorKind::VirtualDeviceMissing;
        return MediaError::make(k, "Cannot open virtual camera " + m_path + ".", std::strerror(e));
    }
    v4l2_capability cap{};
    if (ioctl(m_fd, VIDIOC_QUERYCAP, &cap) != 0 || std::string(reinterpret_cast<char*>(cap.driver)) != "v4l2 loopback") {
        // Refuse anything that is not a loopback device: never write to a
        // physical camera node.
        ::close(m_fd);
        m_fd = -1;
        return MediaError::make(ErrorKind::VirtualDeviceError, m_path + " is not a v4l2loopback device.");
    }
    if (auto e = configure(width, height)) {
        ::close(m_fd);
        m_fd = -1;
        return e;
    }
    return {};
}

MediaError V4L2LoopbackCamera::configure(int width, int height)
{
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = uint32_t(width);
    fmt.fmt.pix.height = uint32_t(height);
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    if (m_pixfmt == PixelFormat::YUV420) {
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
        fmt.fmt.pix.bytesperline = uint32_t(width);
        fmt.fmt.pix.sizeimage = uint32_t(width * height * 3 / 2);
    } else {
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.bytesperline = uint32_t(width * 2);
        fmt.fmt.pix.sizeimage = uint32_t(width * height * 2);
    }
    if (ioctl(m_fd, VIDIOC_S_FMT, &fmt) != 0) {
        int e = errno;
        if (e == EBUSY)
            return MediaError::make(ErrorKind::DeviceBusy,
                                    "Virtual camera " + m_path +
                                        " is busy: another program is feeding it, or a call app is holding it "
                                        "at a different resolution.",
                                    std::strerror(e));
        return MediaError::make(ErrorKind::UnsupportedFormat,
                                "Virtual camera " + m_path + " rejected " + std::to_string(width) + "x" +
                                    std::to_string(height) + ".",
                                std::strerror(e));
    }
    if (int(fmt.fmt.pix.width) != width || int(fmt.fmt.pix.height) != height)
        return MediaError::make(ErrorKind::UnsupportedFormat,
                                "Virtual camera " + m_path + " is locked to " + std::to_string(fmt.fmt.pix.width) +
                                    "x" + std::to_string(fmt.fmt.pix.height) +
                                    " (close apps using it, or match that output resolution).");

    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    parm.parm.output.timeperframe.numerator = 1;
    parm.parm.output.timeperframe.denominator = uint32_t(m_fps);
    ioctl(m_fd, VIDIOC_S_PARM, &parm); // advisory; older modules ignore it

    m_width = width;
    m_height = height;
    m_buf.assign(fmt.fmt.pix.sizeimage, 0);
    m_sws.reset();
    return {};
}

MediaError V4L2LoopbackCamera::convertAndWrite(const AVFrame* f)
{
    const AVPixelFormat dstFmt = m_pixfmt == PixelFormat::YUV420 ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUYV422;
    if (!m_sws || m_swsSrcW != f->width || m_swsSrcH != f->height || m_swsSrcFmt != f->format) {
        m_sws.reset(sws_getContext(f->width, f->height, AVPixelFormat(f->format), m_width, m_height, dstFmt,
                                   SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (!m_sws)
            return MediaError::make(ErrorKind::UnsupportedFormat, "Cannot convert frames for the virtual camera.");
        m_swsSrcW = f->width;
        m_swsSrcH = f->height;
        m_swsSrcFmt = f->format;
    }
    uint8_t* dst[4] = {};
    int dstStride[4] = {};
    if (m_pixfmt == PixelFormat::YUV420) {
        dst[0] = m_buf.data();
        dst[1] = dst[0] + m_width * m_height;
        dst[2] = dst[1] + (m_width / 2) * (m_height / 2);
        dstStride[0] = m_width;
        dstStride[1] = dstStride[2] = m_width / 2;
    } else {
        dst[0] = m_buf.data();
        dstStride[0] = m_width * 2;
    }
    sws_scale(m_sws.get(), f->data, f->linesize, 0, f->height, dst, dstStride);

    ssize_t n = ::write(m_fd, m_buf.data(), m_buf.size());
    if (n < 0) {
        int e = errno;
        if (e == EAGAIN || e == EINTR)
            return {}; // consumer slow: drop this frame
        ErrorKind k = (e == ENODEV || e == EIO) ? ErrorKind::DeviceLost : ErrorKind::VirtualDeviceError;
        return MediaError::make(k, "Writing to virtual camera " + m_path + " failed.", std::strerror(e));
    }
    return {};
}

MediaError V4L2LoopbackCamera::writeFrame(const AVFrame* f)
{
    if (m_fd < 0)
        return MediaError::make(ErrorKind::VirtualDeviceError, "Virtual camera is not open.");
    if (f->width != m_width || f->height != m_height) {
        // Output size changed (user picked a new resolution): v4l2loopback
        // only accepts a new format from a fresh open.
        std::string path = m_path;
        int w = f->width, h = f->height;
        ::close(m_fd);
        m_fd = -1;
        if (auto e = open(path, w, h, m_fps))
            return e;
    }
    return convertAndWrite(f);
}

void V4L2LoopbackCamera::close(const AVFrame* placeholder)
{
    if (m_fd < 0)
        return;
    if (placeholder)
        convertAndWrite(placeholder);
    ::close(m_fd);
    m_fd = -1;
    m_sws.reset();
}

std::string V4L2LoopbackCamera::describe() const
{
    if (m_fd < 0)
        return "not open";
    return m_path + " (" + (m_pixfmt == PixelFormat::YUV420 ? "YUV420 " : "YUYV ") + std::to_string(m_width) + "x" +
           std::to_string(m_height) + ")";
}

} // namespace bcs
