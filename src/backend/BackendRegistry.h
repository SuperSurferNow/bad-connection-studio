#pragma once

#include "backend/VirtualCameraBackend.h"
#include "backend/VirtualMicBackend.h"

#include <memory>
#include <string>
#include <vector>

namespace bcs {

struct BackendOptions {
    bool v4l2Yuyv = false;        // v4l2loopback pixel format YUYV instead of YUV420
    bool showAllAudioOutputs = false;
};

// Platform backends in order of preference. Each create*() returns a fresh
// instance: the UI probes with its own instances, pipelines own theirs.
std::vector<std::string> cameraBackendIds();
std::vector<std::string> micBackendIds();
std::unique_ptr<VirtualCameraBackend> createCameraBackend(const std::string& id, const BackendOptions& opts);
std::unique_ptr<VirtualMicBackend> createMicBackend(const std::string& id, const BackendOptions& opts);

struct CapabilityReport {
    std::string platform; // "Linux (Fedora Linux 44)", "Windows 11 (build 22631)"
    std::vector<BackendStatus> cameras;
    std::vector<BackendStatus> mics;

    const BackendStatus* activeCamera() const; // first available, or null
    const BackendStatus* activeMic() const;
};

// "Fedora Linux 44 / kernel ... / x86_64"
std::string platformDescription();

// Launch-time capability check: read-only, never changes system state.
CapabilityReport probeBackends(const BackendOptions& opts);

// "backendId|deviceId" <-> parts, used for destination combo boxes/settings.
std::string makeDestinationKey(const std::string& backendId, const std::string& deviceId);
bool splitDestinationKey(const std::string& key, std::string& backendId, std::string& deviceId);

} // namespace bcs
