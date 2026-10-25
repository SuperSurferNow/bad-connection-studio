#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bcs::v4l2 {

struct DeviceInfo {
    std::string path;     // /dev/videoN
    std::string stableId; // /dev/v4l/by-id/... when available (survives replug), else path
    std::string card;     // human-readable name
    std::string driver;
    std::string busInfo;
    uint32_t deviceCaps = 0;
    bool isLoopback = false; // v4l2loopback device: never a physical camera
    bool canCapture = false;
    bool canOutput = false;
    bool readable = false;
    bool writable = false;
    int openErrno = 0; // errno if QUERYCAP open failed
};

// Enumerates /dev/video* with VIDIOC_QUERYCAP. Opening a node for QUERYCAP
// does not start streaming and does not change device state.
std::vector<DeviceInfo> scanDevices();

bool loopbackModuleLoaded();         // /sys/module/v4l2loopback exists
std::string loopbackModuleVersion(); // /sys/module/v4l2loopback/version, may be empty
bool userInGroup(const char* group);

} // namespace bcs::v4l2
