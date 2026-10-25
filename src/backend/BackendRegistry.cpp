#include "backend/BackendRegistry.h"

#include "backend/common/AudioDeviceMicBackend.h"

#if defined(BCS_PLATFORM_LINUX)
#include "backend/linux/V4L2LoopbackCamera.h"
#if defined(BCS_WITH_PIPEWIRE)
#include "backend/linux/PipeWireMicBackend.h"
#endif
#elif defined(BCS_PLATFORM_WINDOWS)
#include "backend/windows/SoftcamCameraBackend.h"
#endif

#include <QSysInfo>

namespace bcs {

std::vector<std::string> cameraBackendIds()
{
#if defined(BCS_PLATFORM_LINUX)
    return {"v4l2loopback"};
#elif defined(BCS_PLATFORM_WINDOWS)
    return {"softcam"};
#else
    return {};
#endif
}

std::vector<std::string> micBackendIds()
{
    std::vector<std::string> ids;
#if defined(BCS_WITH_PIPEWIRE)
    ids.push_back("pipewire");
#endif
    ids.push_back("audio-output");
    return ids;
}

std::unique_ptr<VirtualCameraBackend> createCameraBackend(const std::string& id, const BackendOptions& opts)
{
#if defined(BCS_PLATFORM_LINUX)
    if (id == "v4l2loopback")
        return std::make_unique<V4L2LoopbackCamera>(opts.v4l2Yuyv ? V4L2LoopbackCamera::PixelFormat::YUYV
                                                                  : V4L2LoopbackCamera::PixelFormat::YUV420);
#elif defined(BCS_PLATFORM_WINDOWS)
    (void)opts;
    if (id == "softcam")
        return std::make_unique<SoftcamCameraBackend>();
#else
    (void)id;
    (void)opts;
#endif
    return nullptr;
}

std::unique_ptr<VirtualMicBackend> createMicBackend(const std::string& id, const BackendOptions& opts)
{
#if defined(BCS_WITH_PIPEWIRE)
    if (id == "pipewire")
        return std::make_unique<PipeWireMicBackend>();
#endif
    if (id == "audio-output") {
        auto b = std::make_unique<AudioDeviceMicBackend>();
        b->setShowAllOutputs(opts.showAllAudioOutputs);
        return b;
    }
    return nullptr;
}

const BackendStatus* CapabilityReport::activeCamera() const
{
    for (const auto& s : cameras)
        if (s.available)
            return &s;
    return nullptr;
}

const BackendStatus* CapabilityReport::activeMic() const
{
    for (const auto& s : mics)
        if (s.available)
            return &s;
    return nullptr;
}

std::string platformDescription()
{
    return (QSysInfo::prettyProductName() + " / kernel " + QSysInfo::kernelVersion() + " / " +
            QSysInfo::currentCpuArchitecture())
        .toStdString();
}

CapabilityReport probeBackends(const BackendOptions& opts)
{
    CapabilityReport r;
    r.platform = platformDescription();
    for (const auto& id : cameraBackendIds())
        if (auto b = createCameraBackend(id, opts))
            r.cameras.push_back(b->probe());
    for (const auto& id : micBackendIds())
        if (auto b = createMicBackend(id, opts))
            r.mics.push_back(b->probe());
    return r;
}

std::string makeDestinationKey(const std::string& backendId, const std::string& deviceId)
{
    return backendId + "|" + deviceId;
}

bool splitDestinationKey(const std::string& key, std::string& backendId, std::string& deviceId)
{
    auto p = key.find('|');
    if (p == std::string::npos)
        return false;
    backendId = key.substr(0, p);
    deviceId = key.substr(p + 1);
    return true;
}

} // namespace bcs
