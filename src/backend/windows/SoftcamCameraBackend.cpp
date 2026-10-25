#include "backend/windows/SoftcamCameraBackend.h"

#include "core/CameraEnumerator.h"

#include <windows.h>

#include <string>

namespace bcs {

namespace {

constexpr const char* kDeviceId = "softcam";

std::string setupHelpHtml()
{
    return R"html(<p>Windows needs a registered virtual-camera component; a normal
application cannot create a webcam on its own, and FFmpeg output cannot do it
either.</p>
<p>Bad Connection Studio uses <b>Softcam</b> (MIT license), a user-mode
DirectShow camera filter &mdash; not a kernel driver. It is installed by the
separate <b>"Bad Connection Studio Virtual Camera"</b> component installer,
which asks for administrator rights, registers the signed
<code>softcam.dll</code> with <code>regsvr32</code>, and has its own
uninstaller (<i>Settings &gt; Apps</i>). The main installer never registers it
silently.</p>
<ol><li>Run <code>BadConnectionStudio-VirtualCamera-Setup.exe</code>.</li>
<li>Restart call apps so they re-enumerate cameras.</li>
<li>Choose <i>"DirectShow Softcam"</i> in the call app.</li></ol>
<p>Only one program can feed Softcam at a time. Apps that use only the
Media Foundation frame server (e.g. the built-in Camera app) will not list it.</p>)html";
}

} // namespace

SoftcamCameraBackend::SoftcamCameraBackend() = default;

SoftcamCameraBackend::~SoftcamCameraBackend()
{
    close(nullptr);
    // The module is kept loaded for the process lifetime: the DLL owns a
    // shared-memory section that consumers may still reference.
}

bool SoftcamCameraBackend::loadLibrary(std::string& err)
{
    if (m_module)
        return true;
    // Load exactly the DLL the component installer registered with
    // regsvr32 (its path is recorded in HKLM by that installer): the
    // DirectShow filter and the sender must be the same Softcam build.
    // Never search the CWD or PATH.
    HMODULE h = nullptr;
    wchar_t path[MAX_PATH] = {};
    DWORD size = sizeof(path);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\BadConnectionStudio\\VirtualCamera", L"DllPath", RRF_RT_REG_SZ,
                     nullptr, path, &size) == ERROR_SUCCESS)
        h = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) {
        err = "softcam.dll not found (the Virtual Camera component is not installed)";
        return false;
    }
    // FARPROC -> real signature via void* (avoids -Wcast-function-type).
    auto sym = [h](const char* name) { return reinterpret_cast<void*>(GetProcAddress(h, name)); };
    m_create = reinterpret_cast<CreateFn>(sym("scCreateCamera"));
    m_delete = reinterpret_cast<DeleteFn>(sym("scDeleteCamera"));
    m_send = reinterpret_cast<SendFn>(sym("scSendFrame"));
    if (!m_create || !m_delete || !m_send) {
        FreeLibrary(h);
        m_create = nullptr;
        m_delete = nullptr;
        m_send = nullptr;
        err = "softcam.dll does not export the expected API (scCreateCamera/scSendFrame)";
        return false;
    }
    m_module = h;
    return true;
}

BackendStatus SoftcamCameraBackend::probe()
{
    BackendStatus st;
    st.backendId = "softcam";
    st.displayName = "Softcam (DirectShow virtual camera, separate component)";
    st.setupHelp = setupHelpHtml();

    std::string err;
    bool dll = loadLibrary(err);
    // Registered <=> DirectShow enumerates it as a video input device.
    bool registered = false;
    for (const auto& c : enumerateCameras(true))
        if (c.isVirtual && c.name.find("Softcam") != std::string::npos)
            registered = true;

    if (!dll)
        st.summary = err;
    else if (!registered)
        st.summary = "softcam.dll found but not registered as a DirectShow camera (run the component installer)";
    else
        st.summary = "Softcam virtual camera ready";
    st.available = dll && registered;
    if (st.available)
        st.devices.push_back({kDeviceId, "DirectShow Softcam", true, {}});
    return st;
}

MediaError SoftcamCameraBackend::open(const std::string&, int width, int height, int fps)
{
    close(nullptr);
    std::string err;
    if (!loadLibrary(err))
        return MediaError::make(ErrorKind::VirtualDeviceMissing, "The Windows virtual camera is not installed.", err);
    m_camera = m_create(width, height, float(fps > 0 ? fps : 30));
    if (!m_camera)
        return MediaError::make(ErrorKind::DeviceBusy,
                                "The virtual camera is already being fed by another program (Softcam allows one).");
    m_width = width;
    m_height = height;
    m_fps = fps;
    m_bgr.assign(size_t(width) * size_t(height) * 3, 0);
    return {};
}

MediaError SoftcamCameraBackend::convertAndSend(const AVFrame* f)
{
    if (!m_sws || m_swsW != f->width || m_swsH != f->height || m_swsFmt != f->format) {
        m_sws.reset(sws_getContext(f->width, f->height, AVPixelFormat(f->format), m_width, m_height,
                                   AV_PIX_FMT_BGR24, SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (!m_sws)
            return MediaError::make(ErrorKind::UnsupportedFormat, "Cannot convert frames for the virtual camera.");
        m_swsW = f->width;
        m_swsH = f->height;
        m_swsFmt = f->format;
    }
    // Softcam takes top-down 24-bit BGR, width*3 bytes per row.
    uint8_t* dst[4] = {m_bgr.data(), nullptr, nullptr, nullptr};
    int stride[4] = {m_width * 3, 0, 0, 0};
    sws_scale(m_sws.get(), f->data, f->linesize, 0, f->height, dst, stride);
    m_send(m_camera, m_bgr.data());
    return {};
}

MediaError SoftcamCameraBackend::writeFrame(const AVFrame* f)
{
    if (!m_camera)
        return MediaError::make(ErrorKind::VirtualDeviceError, "Virtual camera is not open.");
    if (f->width != m_width || f->height != m_height) {
        // Softcam fixes the size at creation; recreate it.
        int fps = m_fps;
        if (auto e = open(kDeviceId, f->width, f->height, fps))
            return e;
    }
    return convertAndSend(f);
}

void SoftcamCameraBackend::close(const AVFrame* placeholder)
{
    if (!m_camera)
        return;
    if (placeholder)
        convertAndSend(placeholder);
    m_delete(m_camera);
    m_camera = nullptr;
    m_sws.reset();
}

std::string SoftcamCameraBackend::describe() const
{
    if (!m_camera)
        return "not open";
    return "DirectShow Softcam (" + std::to_string(m_width) + "x" + std::to_string(m_height) + ")";
}

} // namespace bcs
