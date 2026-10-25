#pragma once

#include "backend/VirtualCameraBackend.h"
#include "core/FfmpegUtil.h"

#include <vector>

namespace bcs {

// Writes frames to a v4l2loopback output device. The kernel module is an
// external OS component; this class only detects it and explains setup.
class V4L2LoopbackCamera final : public VirtualCameraBackend {
public:
    enum class PixelFormat { YUV420, YUYV };

    explicit V4L2LoopbackCamera(PixelFormat fmt = PixelFormat::YUV420) : m_pixfmt(fmt) {}
    ~V4L2LoopbackCamera() override { close(nullptr); }

    BackendStatus probe() override;
    MediaError open(const std::string& deviceId, int width, int height, int fps) override;
    MediaError writeFrame(const AVFrame* frame) override;
    void close(const AVFrame* placeholder) override;
    bool isOpen() const override { return m_fd >= 0; }
    std::string describe() const override;

    static std::string setupHelpHtml();

private:
    MediaError configure(int width, int height);
    MediaError convertAndWrite(const AVFrame* frame);

    PixelFormat m_pixfmt;
    int m_fd = -1;
    std::string m_path;
    int m_width = 0;
    int m_height = 0;
    int m_fps = 30;
    SwsPtr m_sws;
    int m_swsSrcW = 0, m_swsSrcH = 0, m_swsSrcFmt = -1;
    std::vector<uint8_t> m_buf;
};

} // namespace bcs
