#include "core/CameraEnumerator.h"

#include "core/FfmpegUtil.h"

extern "C" {
#include <libavdevice/avdevice.h>
}

#if defined(BCS_PLATFORM_LINUX)
#include "backend/linux/V4L2Devices.h"
#include <cstring>
#elif defined(__APPLE__)
#include <QCameraDevice>
#include <QMediaDevices>
#endif

#include <algorithm>

namespace bcs {

#if defined(BCS_PLATFORM_LINUX)

std::vector<CameraDevice> enumerateCameras(bool includeVirtual)
{
    std::vector<CameraDevice> out;
    for (const auto& d : v4l2::scanDevices()) {
        if (d.isLoopback && !includeVirtual)
            continue;
        if (d.openErrno == 0 && !d.canCapture)
            continue; // metadata nodes, codecs, output-only devices
        CameraDevice c;
        c.id = d.stableId;
        c.url = d.path;
        c.format = "video4linux2";
        c.isVirtual = d.isLoopback;
        c.name = d.card + (d.isLoopback ? " (virtual)" : "");
        c.accessible = d.openErrno == 0 && d.readable;
        if (d.openErrno)
            c.problem = std::string("cannot open: ") + std::strerror(d.openErrno);
        out.push_back(c);
    }
    return out;
}

#elif defined(__APPLE__)

// libavdevice's avfoundation input cannot list devices (it only logs them),
// so Qt lists them. avfoundation opens a camera by its name.
std::vector<CameraDevice> enumerateCameras(bool includeVirtual)
{
    std::vector<CameraDevice> out;
    for (const QCameraDevice& d : QMediaDevices::videoInputs()) {
        CameraDevice c;
        c.name = d.description().toStdString();
        c.id = d.id().toStdString(); // AVCaptureDevice uniqueID
        c.url = c.name + ":none";    // "<video>:<audio>"
        c.format = "avfoundation";
        // OBS's camera carries our own output when the OBS bridge is used.
        c.isVirtual = c.name.find("OBS Virtual Camera") != std::string::npos ||
                      c.name.find("Bad Connection") != std::string::npos;
        if (c.isVirtual && !includeVirtual)
            continue;
        out.push_back(c);
    }
    return out;
}

#else // Windows (and anything else libavdevice can enumerate)

namespace {
const char* platformInputFormat()
{
    return "dshow";
}

bool looksLikeOurVirtualCamera(const std::string& name)
{
    // Softcam's default DirectShow name, and our own label.
    return name.find("Softcam") != std::string::npos || name.find("Bad Connection") != std::string::npos;
}
} // namespace

std::vector<CameraDevice> enumerateCameras(bool includeVirtual)
{
    std::vector<CameraDevice> out;
    const AVInputFormat* fmt = av_find_input_format(platformInputFormat());
    if (!fmt)
        return out;
    AVDeviceInfoList* list = nullptr;
    if (avdevice_list_input_sources(fmt, nullptr, nullptr, &list) < 0 || !list)
        return out;
    for (int i = 0; i < list->nb_devices; ++i) {
        const AVDeviceInfo* d = list->devices[i];
        bool isVideo = d->nb_media_types == 0;
        for (int m = 0; m < d->nb_media_types; ++m)
            isVideo |= d->media_types[m] == AVMEDIA_TYPE_VIDEO;
        if (!isVideo)
            continue;
        CameraDevice c;
        c.name = d->device_description ? d->device_description : d->device_name;
        c.id = d->device_name; // dshow: unique "@device_pnp_..." moniker
        c.url = std::string("video=") + d->device_name;
        c.format = platformInputFormat();
        c.isVirtual = looksLikeOurVirtualCamera(c.name);
        if (c.isVirtual && !includeVirtual)
            continue;
        out.push_back(c);
    }
    avdevice_free_list_devices(&list);
    return out;
}

#endif

std::optional<CameraDevice> findCamera(const std::string& id)
{
    for (auto& c : enumerateCameras(true))
        if (c.id == id || c.url == id)
            return c;
    return std::nullopt;
}

} // namespace bcs
