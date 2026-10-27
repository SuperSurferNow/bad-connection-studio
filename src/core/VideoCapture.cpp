#include "core/VideoCapture.h"

extern "C" {
#include <libavdevice/avdevice.h>
}

#include <cerrno>

namespace bcs {

CameraDevice testPatternCamera()
{
    CameraDevice c;
    c.id = kTestPatternCameraId;
    c.name = "Test pattern (generated, no camera)";
    c.format = "synthetic";
    return c;
}

namespace {

ErrorKind kindFromAvError(int r)
{
    if (r == AVERROR(EACCES) || r == AVERROR(EPERM))
        return ErrorKind::PermissionDenied;
    if (r == AVERROR(EBUSY))
        return ErrorKind::DeviceBusy;
    if (r == AVERROR(ENOENT) || r == AVERROR(ENXIO))
        return ErrorKind::DeviceNotFound;
    if (r == AVERROR(ENODEV) || r == AVERROR(EIO))
        return ErrorKind::DeviceLost;
    if (r == AVERROR(EINVAL) || r == AVERROR_PATCHWELCOME || r == AVERROR_DECODER_NOT_FOUND)
        return ErrorKind::UnsupportedFormat;
    return ErrorKind::Other;
}

} // namespace

MediaError VideoCapture::open(const CameraDevice& device, const CaptureRequest& req)
{
    close();
    if (device.format == "synthetic") {
        int w = req.width > 0 ? req.width : 1280;
        int h = req.height > 0 ? req.height : 720;
        m_synthetic = std::make_unique<SyntheticVideo>(w, h);
        m_synthFps = req.fps > 0 ? req.fps : 30;
        m_synthIndex = 0;
        m_synthNextUs = nowUs();
        m_description = std::to_string(w) + "x" + std::to_string(h) + " test pattern @ " +
                        std::to_string(m_synthFps) + " fps";
        return {};
    }

    // Try the exact request first, then relax it: many cameras only offer
    // some size/format combinations. Falling back is better than failing.
    MediaError err = tryOpen(device, req, true, true);
    if (err && err.kind != ErrorKind::PermissionDenied && err.kind != ErrorKind::DeviceBusy &&
        err.kind != ErrorKind::DeviceNotFound) {
        if (!req.inputFormat.empty())
            err = tryOpen(device, req, true, false);
        if (err)
            err = tryOpen(device, req, false, false);
    }
    return err;
}

MediaError VideoCapture::tryOpen(const CameraDevice& device, const CaptureRequest& req, bool withSize, bool withFormat)
{
    close();
    const AVInputFormat* ifmt = av_find_input_format(device.format.c_str());
    if (!ifmt)
        return MediaError::make(ErrorKind::Other,
                                "Camera capture via '" + device.format + "' is not available in this FFmpeg build.");

    AVDictionary* opts = nullptr;
    if (withSize && req.width > 0 && req.height > 0) {
        av_dict_set(&opts, "video_size", (std::to_string(req.width) + "x" + std::to_string(req.height)).c_str(), 0);
        av_dict_set(&opts, "framerate", std::to_string(req.fps).c_str(), 0);
    }
    if (withFormat && !req.inputFormat.empty()) {
        if (device.format == "dshow")
            av_dict_set(&opts, req.inputFormat == "mjpeg" ? "vcodec" : "pixel_format", req.inputFormat.c_str(), 0);
        else
            av_dict_set(&opts, "input_format", req.inputFormat.c_str(), 0);
    }
    if (device.format == "dshow")
        av_dict_set(&opts, "rtbufsize", "8M", 0); // bounded device-side buffer
    if (device.format == "avfoundation") {
        // avfoundation's defaults (29.97 fps, yuv420p) are refused by most
        // Mac cameras, so even the relaxed attempts name a rate and nv12,
        // which every built-in and UVC camera offers.
        if (!av_dict_get(opts, "framerate", nullptr, 0))
            av_dict_set(&opts, "framerate", std::to_string(req.fps > 0 ? req.fps : 30).c_str(), 0);
        if (!av_dict_get(opts, "pixel_format", nullptr, 0))
            av_dict_set(&opts, "pixel_format", "nv12", 0);
    }

    m_fmt = avformat_alloc_context();
    m_fmt->flags |= AVFMT_FLAG_NONBLOCK | AVFMT_FLAG_NOBUFFER;
    int r = avformat_open_input(&m_fmt, device.url.c_str(), ifmt, &opts); // frees m_fmt on failure
    av_dict_free(&opts);
    if (r < 0) {
        m_fmt = nullptr;
        ErrorKind k = kindFromAvError(r);
        std::string msg;
        switch (k) {
        case ErrorKind::PermissionDenied: msg = "No permission to open camera '" + device.name + "'."; break;
        case ErrorKind::DeviceBusy: msg = "Camera '" + device.name + "' is in use by another application."; break;
        case ErrorKind::DeviceNotFound:
        case ErrorKind::DeviceLost: msg = "Camera '" + device.name + "' is not connected."; break;
        default: msg = "Camera '" + device.name + "' could not be opened with the requested format."; break;
        }
        return MediaError::make(k, msg, averr(r));
    }

    m_stream = -1;
    for (unsigned i = 0; i < m_fmt->nb_streams; ++i)
        if (m_fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_stream = int(i);
            break;
        }
    if (m_stream < 0) {
        close();
        return MediaError::make(ErrorKind::UnsupportedFormat, "The camera did not provide a video stream.");
    }
    const AVCodecParameters* par = m_fmt->streams[m_stream]->codecpar;
    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    if (!dec) {
        close();
        return MediaError::make(ErrorKind::UnsupportedFormat, "The camera's pixel format cannot be decoded.",
                                avcodec_get_name(par->codec_id));
    }
    m_dec.reset(avcodec_alloc_context3(dec));
    avcodec_parameters_to_context(m_dec.get(), par);
    m_dec->thread_count = 1;
    m_dec->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if ((r = avcodec_open2(m_dec.get(), dec, nullptr)) < 0) {
        close();
        return MediaError::make(ErrorKind::UnsupportedFormat, "Could not decode the camera's format.", averr(r));
    }
    m_pkt = makePacket();

    AVRational fr = m_fmt->streams[m_stream]->avg_frame_rate;
    const char* rawName = av_get_pix_fmt_name(AVPixelFormat(par->format));
    std::string pix = par->codec_id == AV_CODEC_ID_RAWVIDEO ? std::string(rawName ? rawName : "raw")
                                                            : std::string(avcodec_get_name(par->codec_id));
    m_description = std::to_string(par->width) + "x" + std::to_string(par->height) + " " + pix;
    if (fr.num > 0 && fr.den > 0)
        m_description += " @ " + std::to_string(int(av_q2d(fr) + 0.5)) + " fps";
    if (!withSize)
        m_description += " (camera default mode)";
    return {};
}

void VideoCapture::close()
{
    m_dec.reset();
    m_pkt.reset();
    if (m_fmt)
        avformat_close_input(&m_fmt);
    m_synthetic.reset();
}

int VideoCapture::read(FramePtr& out, MediaError& err)
{
    if (m_synthetic) {
        int64_t now = nowUs();
        if (now < m_synthNextUs)
            return 0;
        m_synthNextUs = std::max(m_synthNextUs + 1000000 / m_synthFps, now - 100000);
        out = m_synthetic->frame(m_synthIndex++);
        return out ? 1 : 0;
    }
    if (!m_fmt) {
        err = MediaError::make(ErrorKind::DeviceLost, "Camera is not open.");
        return -1;
    }
    int r = av_read_frame(m_fmt, m_pkt.get());
    if (r == AVERROR(EAGAIN))
        return 0;
    if (r < 0) {
        // Any read error on a live device means it went away (unplug,
        // suspend, driver reset). The pipeline reconnects.
        err = MediaError::make(ErrorKind::DeviceLost, "The camera stopped delivering frames.", averr(r));
        return -1;
    }
    if (m_pkt->stream_index != m_stream) {
        av_packet_unref(m_pkt.get());
        return 0;
    }
    r = avcodec_send_packet(m_dec.get(), m_pkt.get());
    av_packet_unref(m_pkt.get());
    if (r < 0 && r != AVERROR(EAGAIN))
        return 0; // a corrupt MJPEG frame is not fatal
    FramePtr f = makeFrame();
    r = avcodec_receive_frame(m_dec.get(), f.get());
    if (r < 0)
        return 0;
    out = std::move(f);
    return 1;
}

} // namespace bcs
