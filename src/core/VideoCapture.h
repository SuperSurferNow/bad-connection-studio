#pragma once

#include "core/CameraEnumerator.h"
#include "core/Errors.h"
#include "core/FfmpegUtil.h"
#include "core/SyntheticSource.h"

#include <memory>
#include <string>

namespace bcs {

// Pseudo device id for the built-in generated test pattern.
inline constexpr const char* kTestPatternCameraId = "bcs:test-pattern";
CameraDevice testPatternCamera();

struct CaptureRequest {
    int width = 1280; // 0 = camera default
    int height = 720;
    int fps = 30;
    std::string inputFormat; // "" (auto), "mjpeg", "yuyv422"
};

// Reads raw frames from a physical camera with libavdevice (v4l2 / dshow)
// and decodes them (MJPEG or raw) to AVFrames. Non-blocking: read() returns
// 0 when no frame is ready so the worker can notice stop requests.
class VideoCapture {
public:
    VideoCapture() = default;
    ~VideoCapture() { close(); }
    VideoCapture(const VideoCapture&) = delete;
    VideoCapture& operator=(const VideoCapture&) = delete;

    MediaError open(const CameraDevice& device, const CaptureRequest& req);
    void close();
    bool isOpen() const { return m_fmt != nullptr || m_synthetic != nullptr; }

    // 1: frame returned, 0: nothing yet, -1: error (see err).
    int read(FramePtr& out, MediaError& err);

    // e.g. "1280x720 mjpeg @ 30 fps"
    const std::string& description() const { return m_description; }

private:
    MediaError tryOpen(const CameraDevice& device, const CaptureRequest& req, bool withSize, bool withFormat);

    AVFormatContext* m_fmt = nullptr;
    CodecCtxPtr m_dec;
    PacketPtr m_pkt;
    int m_stream = -1;
    std::string m_description;

    std::unique_ptr<SyntheticVideo> m_synthetic;
    int64_t m_synthIndex = 0;
    int64_t m_synthNextUs = 0;
    int m_synthFps = 30;
};

} // namespace bcs
