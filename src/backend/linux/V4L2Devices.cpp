#include "backend/linux/V4L2Devices.h"

#include <linux/videodev2.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include <fcntl.h>
#include <grp.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace bcs::v4l2 {

namespace {

std::map<std::string, std::string> byIdLinks()
{
    std::map<std::string, std::string> out; // canonical /dev/videoN -> by-id link
    std::error_code ec;
    for (const char* dir : {"/dev/v4l/by-id", "/dev/v4l/by-path"}) {
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            auto target = fs::canonical(e.path(), ec);
            if (ec)
                continue;
            out.emplace(target.string(), e.path().string()); // by-id wins (inserted first)
        }
    }
    return out;
}

int videoIndex(const std::string& name)
{
    try {
        return std::stoi(name.substr(5)); // "videoN"
    } catch (...) {
        return 1 << 20;
    }
}

} // namespace

std::vector<DeviceInfo> scanDevices()
{
    std::vector<std::string> nodes;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/dev", ec)) {
        std::string n = e.path().filename().string();
        if (n.rfind("video", 0) == 0 && n.size() > 5 && std::isdigit(static_cast<unsigned char>(n[5])))
            nodes.push_back(n);
    }
    std::sort(nodes.begin(), nodes.end(), [](auto& a, auto& b) { return videoIndex(a) < videoIndex(b); });

    auto links = byIdLinks();
    std::vector<DeviceInfo> out;
    for (const auto& n : nodes) {
        DeviceInfo d;
        d.path = "/dev/" + n;
        d.readable = access(d.path.c_str(), R_OK) == 0;
        d.writable = access(d.path.c_str(), W_OK) == 0;
        auto it = links.find(d.path);
        d.stableId = it != links.end() ? it->second : d.path;

        int fd = ::open(d.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            d.openErrno = errno;
            d.card = n;
            out.push_back(d);
            continue;
        }
        v4l2_capability cap{};
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            d.card = reinterpret_cast<const char*>(cap.card);
            d.driver = reinterpret_cast<const char*>(cap.driver);
            d.busInfo = reinterpret_cast<const char*>(cap.bus_info);
            d.deviceCaps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
            d.isLoopback = d.driver == "v4l2 loopback" || d.busInfo.rfind("platform:v4l2loopback", 0) == 0;
            d.canCapture = d.deviceCaps & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE);
            d.canOutput = d.deviceCaps & (V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_MPLANE);
        } else {
            d.openErrno = errno;
            d.card = n;
        }
        ::close(fd);
        out.push_back(d);
    }
    return out;
}

bool loopbackModuleLoaded()
{
    std::error_code ec;
    return fs::exists("/sys/module/v4l2loopback", ec);
}

std::string loopbackModuleVersion()
{
    std::ifstream f("/sys/module/v4l2loopback/version");
    std::string v;
    std::getline(f, v);
    return v;
}

bool userInGroup(const char* group)
{
    const ::group* g = getgrnam(group);
    if (!g)
        return false;
    int n = getgroups(0, nullptr);
    if (n <= 0)
        return getegid() == g->gr_gid;
    std::vector<gid_t> gids(static_cast<std::size_t>(n));
    n = getgroups(n, gids.data());
    gids.resize(static_cast<std::size_t>(std::max(n, 0)));
    return getegid() == g->gr_gid || std::find(gids.begin(), gids.end(), g->gr_gid) != gids.end();
}

} // namespace bcs::v4l2
