#pragma once

#include "backend/VirtualCameraBackend.h"
#include "core/FfmpegUtil.h"

#include <vector>

namespace bcs {

// Windows virtual camera through Softcam (https://github.com/tshino/softcam,
// MIT license): a user-mode DirectShow source filter DLL. The DLL must be
// registered (regsvr32) by the separate, signed "Bad Connection Studio
// Virtual Camera" component installer; this backend only loads the same DLL
// at runtime to feed frames to it and never registers anything itself.
//
// Visibility: DirectShow cameras are listed by Zoom, Teams (classic/new
// desktop), Discord, OBS, Chrome/Edge and Firefox desktop. Apps that only use
// Media Foundation's frame server (e.g. the Windows Camera app) do not see
// DirectShow cameras; that needs the Windows 11 MFCreateVirtualCamera path
// documented in docs/WINDOWS_SETUP.md.
class SoftcamCameraBackend final : public VirtualCameraBackend {
public:
    SoftcamCameraBackend();
    ~SoftcamCameraBackend() override;

    BackendStatus probe() override;
    MediaError open(const std::string& deviceId, int width, int height, int fps) override;
    MediaError writeFrame(const AVFrame* frame) override;
    void close(const AVFrame* placeholder) override;
    bool isOpen() const override { return m_camera != nullptr; }
    std::string describe() const override;

private:
    bool loadLibrary(std::string& err);
    MediaError convertAndSend(const AVFrame* frame);

    using CreateFn = void* (*)(int, int, float);
    using DeleteFn = void (*)(void*);
    using SendFn = void (*)(void*, const void*);

    void* m_module = nullptr; // HMODULE
    CreateFn m_create = nullptr;
    DeleteFn m_delete = nullptr;
    SendFn m_send = nullptr;

    void* m_camera = nullptr;
    int m_width = 0, m_height = 0, m_fps = 30;
    SwsPtr m_sws;
    int m_swsW = 0, m_swsH = 0, m_swsFmt = -1;
    std::vector<uint8_t> m_bgr;
};

} // namespace bcs
