#pragma once

#include "core/Errors.h"

#include <string>
#include <vector>

struct AVFrame;

namespace bcs {

// One destination a backend can publish to (e.g. a specific /dev/videoN).
struct VirtualDeviceInfo {
    std::string id;   // stable id saved in preferences
    std::string name; // display name
    bool usable = true;
    std::string problem; // why it is not usable right now
};

// Result of a backend's launch-time capability check.
struct BackendStatus {
    std::string backendId;   // "v4l2loopback", "pipewire", "softcam", ...
    std::string displayName; // "v4l2loopback (Linux kernel module)"
    bool available = false;  // at least one usable destination
    std::string summary;     // one line, shown in the status bar / diagnostics
    std::string setupHelp;   // HTML: how to install / grant access (never auto-run)
    std::vector<VirtualDeviceInfo> devices;
};

// Publishes decoded frames as an OS-visible webcam. Implementations wrap an
// EXTERNAL OS component (kernel module, registered COM filter); they never
// install or load it themselves, and never touch physical cameras.
//
// Thread model: probe() from the UI thread; open/writeFrame/close from the
// video output thread only.
class VirtualCameraBackend {
public:
    virtual ~VirtualCameraBackend() = default;

    virtual BackendStatus probe() = 0;

    virtual MediaError open(const std::string& deviceId, int width, int height, int fps) = 0;
    // Any pixel format; the backend converts to what the device needs. If the
    // frame size differs from open(), the backend reconfigures itself.
    virtual MediaError writeFrame(const AVFrame* frame) = 0;
    // Shows `placeholder` (if given) so call apps do not freeze on the last
    // camera image, then releases the device.
    virtual void close(const AVFrame* placeholder) = 0;
    virtual bool isOpen() const = 0;
    virtual std::string describe() const = 0; // "/dev/video10 (YUV420 1280x720)"
};

} // namespace bcs
