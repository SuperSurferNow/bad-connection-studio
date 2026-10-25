#pragma once

#include <optional>
#include <string>
#include <vector>

namespace bcs {

// A physical (or, if explicitly requested, virtual) camera usable as input.
struct CameraDevice {
    std::string id;     // stable identifier saved in preferences
    std::string name;   // display name
    std::string url;    // avformat_open_input URL
    std::string format; // libavdevice input format ("video4linux2", "dshow")
    bool isVirtual = false;
    bool accessible = true;
    std::string problem; // why it may not be usable (permissions, ...)
};

// Physical cameras. Virtual cameras (v4l2loopback / our own Softcam output)
// are excluded unless includeVirtual is set, so the app cannot feed its own
// output back into itself by default.
std::vector<CameraDevice> enumerateCameras(bool includeVirtual = false);

// Re-resolves a saved id to the device's current URL (after replug the
// /dev/videoN number may change, the by-id link does not).
std::optional<CameraDevice> findCamera(const std::string& id);

} // namespace bcs
