#pragma once

#include "backend/VirtualCameraBackend.h"
#include "core/FfmpegUtil.h"

#include <QImage>
#include <QSize>

#include <memory>
#include <mutex>

namespace bcs {

class OutputWindow;

// "OBS bridge": shows the degraded video in a plain, borderless window titled
// "Bad Connection Studio — Output". OBS captures that window (Window Capture)
// and publishes it with its own Virtual Camera, which call apps then select.
//
// Needs no driver or OS component, so it works on every platform. On macOS it
// is the only camera backend (a built-in camera needs a paid Apple developer
// entitlement); on Linux and Windows it is a fallback.
//
// Thread model: the window lives on the GUI thread. open()/writeFrame()/
// close() may run on any one thread at a time (as VirtualCameraBackend
// requires); they only post to the GUI thread, never wait for it. A busy GUI
// thread drops frames instead of queueing them.
class WindowOutputBackend final : public VirtualCameraBackend {
public:
    static constexpr const char* kId = "window";
    static constexpr const char* kWindowTitle = "Bad Connection Studio — Output";

    WindowOutputBackend();
    ~WindowOutputBackend() override;

    BackendStatus probe() override;
    MediaError open(const std::string& deviceId, int width, int height, int fps) override;
    MediaError writeFrame(const AVFrame* frame) override;
    void close(const AVFrame* placeholder) override;
    bool isOpen() const override;
    std::string describe() const override;

    static std::string setupHelpHtml();

private:
    QImage convert(const AVFrame* f);

    int m_width = 0, m_height = 0;
    bool m_open = false;

    SwsPtr m_sws;
    int m_swsW = 0, m_swsH = 0, m_swsFmt = -1;

    // Shared with the GUI thread, which owns the window. Work is only ever
    // posted to it, never waited for: the output thread may (re)open the
    // window while the GUI thread is stopping the pipeline.
    struct Shared {
        std::mutex mutex;
        OutputWindow* window = nullptr; // GUI thread only
        bool wantOpen = false;
        QSize size;
        QImage image; // newest frame not yet shown
        bool frameScheduled = false;
    };
    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
};

} // namespace bcs
