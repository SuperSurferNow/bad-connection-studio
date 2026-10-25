#include "core/Errors.h"

#include <cerrno>

namespace bcs {

const char* errorKindTitle(ErrorKind kind)
{
    switch (kind) {
    case ErrorKind::None: return "OK";
    case ErrorKind::PermissionDenied: return "Permission denied";
    case ErrorKind::DeviceBusy: return "Device busy";
    case ErrorKind::DeviceNotFound: return "Device not found";
    case ErrorKind::DeviceLost: return "Device disconnected";
    case ErrorKind::UnsupportedFormat: return "Unsupported format";
    case ErrorKind::CodecInit: return "Codec initialization failed";
    case ErrorKind::VirtualDeviceMissing: return "Virtual device driver missing";
    case ErrorKind::VirtualDeviceError: return "Virtual device error";
    case ErrorKind::Other: return "Error";
    }
    return "Error";
}

const char* errorKindHint(ErrorKind kind)
{
    switch (kind) {
    case ErrorKind::PermissionDenied:
#if defined(_WIN32)
        return "Check Settings > Privacy & security > Camera / Microphone and allow desktop apps.";
#else
        return "Your user cannot open the device node. Check that you are logged in locally "
               "(udev 'uaccess') or a member of the 'video' / 'audio' group. See Help > Diagnostics.";
#endif
    case ErrorKind::DeviceBusy:
        return "Another application is using the device exclusively. Close it (or its "
               "virtual camera output) and try again.";
    case ErrorKind::DeviceNotFound:
        return "Reconnect the device or pick a different one. The list refreshes automatically.";
    case ErrorKind::DeviceLost:
        return "The device stopped responding. Bad Connection Studio will reconnect when it returns.";
    case ErrorKind::UnsupportedFormat:
        return "Try another capture resolution / frame rate in the advanced settings.";
    case ErrorKind::CodecInit:
        return "This codec is not available in the bundled FFmpeg build or rejected the "
               "settings. Pick another codec in the advanced settings.";
    case ErrorKind::VirtualDeviceMissing:
        return "The OS-level virtual device component is not installed or not loaded. "
               "Open Help > Diagnostics for setup instructions. The preview still works.";
    case ErrorKind::VirtualDeviceError:
        return "Writing to the virtual device failed. Stop and start the stream, or "
               "re-create the virtual device (see Help > Diagnostics).";
    default:
        return "";
    }
}

ErrorKind errorKindFromErrno(int err)
{
    switch (err) {
    case EACCES:
    case EPERM: return ErrorKind::PermissionDenied;
    case EBUSY: return ErrorKind::DeviceBusy;
    case ENOENT:
    case ENXIO: return ErrorKind::DeviceNotFound;
    case ENODEV:
    case EIO: return ErrorKind::DeviceLost;
    case EINVAL: return ErrorKind::UnsupportedFormat;
    default: return ErrorKind::Other;
    }
}

} // namespace bcs
