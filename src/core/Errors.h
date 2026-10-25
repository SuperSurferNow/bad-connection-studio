#pragma once

#include <string>

namespace bcs {

// Categories the UI turns into actionable explanations. Keep this list in sync
// with errorKindTitle() / errorKindHint().
enum class ErrorKind {
    None,
    PermissionDenied,     // OS refused access to a device node / privacy setting
    DeviceBusy,           // another process holds the device exclusively
    DeviceNotFound,       // selected device does not exist (yet)
    DeviceLost,           // device disappeared while streaming (unplug, suspend)
    UnsupportedFormat,    // device cannot deliver/accept the requested format
    CodecInit,            // encoder/decoder could not be created or configured
    VirtualDeviceMissing, // OS-level virtual device component is not installed/loaded
    VirtualDeviceError,   // virtual device present but writing to it failed
    Other,
};

struct MediaError {
    ErrorKind kind = ErrorKind::None;
    std::string message; // short, user-facing
    std::string detail;  // technical (errno / FFmpeg error string); logged

    explicit operator bool() const { return kind != ErrorKind::None; }
    static MediaError make(ErrorKind k, std::string msg, std::string det = {})
    {
        return MediaError{k, std::move(msg), std::move(det)};
    }
};

const char* errorKindTitle(ErrorKind kind);
// One or two sentences on what the user can do about it.
const char* errorKindHint(ErrorKind kind);

// Map an errno value from a device open/ioctl to an ErrorKind.
ErrorKind errorKindFromErrno(int err);

} // namespace bcs
