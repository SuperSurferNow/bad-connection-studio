#include "backend/linux/LoopbackSetup.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <sys/utsname.h>

namespace fs = std::filesystem;

namespace bcs::loopback {

namespace {

const char* const kRpmFusionUrl =
    "https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm";

std::string readFile(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string trim(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        ++i;
    return s.substr(i);
}

std::string unquote(std::string v)
{
    v = trim(v);
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
        v = v.substr(1, v.size() - 2);
    return v;
}

bool hasWord(const std::string& list, const std::string& word)
{
    std::istringstream ss(list);
    std::string w;
    while (ss >> w)
        if (w == word)
            return true;
    return false;
}

bool pathExists(const fs::path& p)
{
    std::error_code ec;
    return fs::exists(p, ec);
}

fs::path under(const std::string& root, const std::string& abs)
{
    return fs::path(root) / fs::path(abs).relative_path();
}

std::string unameRelease()
{
    utsname u{};
    if (uname(&u) != 0)
        return {};
    return u.release;
}

std::vector<long> numericRuns(const std::string& s)
{
    std::vector<long> out;
    long cur = -1;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
        } else if (cur >= 0) {
            out.push_back(cur);
            cur = -1;
        }
    }
    if (cur >= 0)
        out.push_back(cur);
    return out;
}

bool isLoopbackModuleFile(const fs::path& p)
{
    return p.filename().string().rfind("v4l2loopback.ko", 0) == 0;
}

} // namespace

// ------------------------------------------------------------------ distro

DistroInfo parseOsRelease(const std::string& text, bool ostreeBooted)
{
    DistroInfo d;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#')
            continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = unquote(line.substr(eq + 1));
        if (key == "ID")
            d.id = val;
        else if (key == "ID_LIKE")
            d.idLike = val;
        else if (key == "VERSION_ID")
            d.versionId = val;
        else if (key == "VARIANT_ID")
            d.variantId = val;
        else if (key == "PRETTY_NAME")
            d.prettyName = val;
    }
    auto is = [&](const char* name) { return d.id == name || hasWord(d.idLike, name); };
    if (d.id == "steamos")
        d.family = Family::SteamOS;
    else if (is("fedora"))
        d.family = ostreeBooted ? Family::FedoraAtomic : Family::Fedora;
    else if (is("arch"))
        d.family = Family::Arch;
    else if (is("debian") || is("ubuntu"))
        d.family = Family::Debian;
    else if (d.id.rfind("opensuse", 0) == 0 || is("suse") || is("opensuse"))
        d.family = Family::OpenSuse;
    if (d.prettyName.empty())
        d.prettyName = d.id.empty() ? "Unknown Linux" : d.id;
    return d;
}

DistroInfo detectDistro(const std::string& root)
{
    std::string text = readFile(under(root, "/etc/os-release"));
    if (text.empty())
        text = readFile(under(root, "/usr/lib/os-release"));
    return parseOsRelease(text, pathExists(under(root, "/run/ostree-booted")));
}

const char* familyName(Family f)
{
    switch (f) {
    case Family::Fedora: return "Fedora";
    case Family::FedoraAtomic: return "Fedora Atomic";
    case Family::Arch: return "Arch";
    case Family::Debian: return "Debian/Ubuntu";
    case Family::OpenSuse: return "openSUSE";
    case Family::SteamOS: return "SteamOS";
    case Family::Unknown: break;
    }
    return "unknown";
}

// ------------------------------------------------------------------ kernel

int compareKernelVersions(const std::string& a, const std::string& b)
{
    auto x = numericRuns(a), y = numericRuns(b);
    for (size_t i = 0; i < std::min(x.size(), y.size()); ++i)
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    if (x.size() != y.size())
        return x.size() < y.size() ? -1 : 1;
    return 0;
}

std::string kernelFlavor(const std::string& release)
{
    std::string out;
    for (char c : release) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            if (out.empty() || out.back() != '#')
                out += '#';
        } else {
            out += c;
        }
    }
    return out;
}

KernelInfo detectKernel(const std::string& root, const std::string& running)
{
    KernelInfo k;
    k.running = running.empty() ? unameRelease() : running;
    const fs::path modules = under(root, "/usr/lib/modules");
    const fs::path boot = under(root, "/boot");
    const fs::path mine = modules / k.running;

    // A kernel counts as installed only if its image is still there: package
    // updates leave behind module directories of removed kernels.
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(modules, ec)) {
        const std::string rel = e.path().filename().string();
        if (pathExists(e.path() / "vmlinuz") || pathExists(boot / ("vmlinuz-" + rel)))
            k.installed.push_back(rel);
    }
    std::sort(k.installed.begin(), k.installed.end(),
              [](const std::string& a, const std::string& b) { return compareKernelVersions(a, b) < 0; });

    k.pkgbase = trim(readFile(mine / "pkgbase"));
    const std::string flavor = kernelFlavor(k.running);
    for (const auto& rel : k.installed) {
        bool same = k.pkgbase.empty() ? kernelFlavor(rel) == flavor
                                      : trim(readFile(modules / rel / "pkgbase")) == k.pkgbase;
        if (same && (k.newestSameFlavor.empty() || compareKernelVersions(rel, k.newestSameFlavor) > 0))
            k.newestSameFlavor = rel;
    }

    k.modulesDirPresent = pathExists(mine / "kernel");
    k.headersPresent = pathExists(mine / "build");
    if (k.modulesDirPresent) {
        for (auto it = fs::recursive_directory_iterator(mine, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file(ec) && isLoopbackModuleFile(it->path())) {
                k.moduleFilePresent = true;
                break;
            }
        }
    }

    // akmod (Fedora) or dkms (Arch/Debian) source installed.
    ec.clear();
    if (pathExists(under(root, "/usr/src/akmods/v4l2loopback-kmod.latest")))
        k.sourcePresent = true;
    for (const auto& e : fs::directory_iterator(under(root, "/usr/src"), ec))
        if (e.path().filename().string().rfind("v4l2loopback-", 0) == 0)
            k.sourcePresent = true;

    // EFI variable: 4 attribute bytes, then 1 data byte.
    std::string sb = readFile(
        under(root, "/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c"));
    k.secureBoot = sb.size() >= 5 && sb[4] == 1;
    return k;
}

// ------------------------------------------------------------------ state

State evaluate(bool moduleLoaded, const DistroInfo& d, const KernelInfo& k)
{
    if (moduleLoaded)
        return State::Ready;
    if (k.moduleFilePresent)
        return State::InstalledNotLoaded;
    if (!k.modulesDirPresent)
        return State::NeedsReboot;
    if (d.family == Family::Unknown || d.family == Family::SteamOS)
        return State::Unsupported;
    if (!k.newestSameFlavor.empty() && compareKernelVersions(k.running, k.newestSameFlavor) < 0)
        return State::KernelOutdated;
    return k.sourcePresent ? State::NotBuilt : State::NotInstalled;
}

const char* stateName(State s)
{
    switch (s) {
    case State::Ready: return "ready";
    case State::InstalledNotLoaded: return "installed-not-loaded";
    case State::NotBuilt: return "not-built";
    case State::NotInstalled: return "not-installed";
    case State::KernelOutdated: return "kernel-outdated";
    case State::NeedsReboot: return "needs-reboot";
    case State::Unsupported: return "unsupported";
    }
    return "?";
}

std::string describeState(State s, const DistroInfo& d, const KernelInfo& k)
{
    switch (s) {
    case State::Ready:
        return "The v4l2loopback virtual camera driver is loaded.";
    case State::InstalledNotLoaded:
        return "The v4l2loopback driver is installed but not loaded. Loading it takes a few seconds.";
    case State::NotBuilt:
        return "The v4l2loopback package is installed, but the driver has not been built for your running "
               "kernel (" + k.running + ").";
    case State::NotInstalled:
        return "The v4l2loopback virtual camera driver is not installed.";
    case State::KernelOutdated:
        return "A newer kernel (" + k.newestSameFlavor + ") is installed than the one you are running (" +
               k.running + "). Restart your computer first: building the driver for the old kernel often fails "
               "because its headers are no longer available, and the work would be lost at the next restart.";
    case State::NeedsReboot:
        return "Your kernel was updated and the files of the running kernel (" + k.running +
               ") have been removed. Restart your computer, then set up the virtual camera.";
    case State::Unsupported:
        if (d.family == Family::SteamOS)
            return "SteamOS has a read-only system partition, so the driver cannot be installed automatically.";
        return "Automatic setup is not available for " + d.prettyName + ". Install the v4l2loopback package "
               "with your distribution's package manager.";
    }
    return {};
}

// ------------------------------------------------------------------ plan

const char* moduleOptions()
{
    return "devices=1 video_nr=10 card_label=\"Bad Connection Camera\" exclusive_caps=1";
}

const std::vector<std::string>& allowedActions()
{
    static const std::vector<std::string> a = {
        "enable-rpmfusion", "install-fedora", "install-fedora-atomic", "install-arch",
        "install-debian",   "install-opensuse", "build-module", "load-module", "persist",
    };
    return a;
}

bool isAllowedAction(const std::string& a)
{
    const auto& all = allowedActions();
    return std::find(all.begin(), all.end(), a) != all.end();
}

std::vector<std::string> InstallPlan::actions() const
{
    std::vector<std::string> out;
    for (const auto& s : steps)
        out.push_back(s.action);
    return out;
}

InstallPlan buildPlan(const DistroInfo& d, const KernelInfo& k, State s, bool persist)
{
    InstallPlan p;
    const std::string load = std::string("modprobe v4l2loopback ") + moduleOptions();

    if (s == State::Ready) {
        p.blocker = "Nothing to do: the virtual camera driver is already loaded.";
        if (persist) {
            p.runnable = true;
            p.blocker.clear();
        }
    } else if (s == State::NeedsReboot) {
        p.blocker = "Restart your computer, then open Bad Connection Studio again.";
        return p;
    } else if (s == State::InstalledNotLoaded) {
        p.runnable = true;
        p.steps.push_back({"load-module", "Load the driver", {load}});
    } else if (s == State::Unsupported) {
        p.blocker = describeState(s, d, k);
        p.notes.push_back("Install the v4l2loopback (or v4l2loopback-dkms) package and the headers of your "
                          "running kernel, then run: sudo " + load);
        return p;
    } else {
        p.runnable = true;
        switch (d.family) {
        case Family::Fedora:
            p.steps.push_back({"enable-rpmfusion", "Enable the RPM Fusion \"free\" repository (if not enabled)",
                               {std::string("dnf install -y ") + kRpmFusionUrl}});
            p.steps.push_back({"install-fedora", "Install the driver package and kernel headers",
                               {"dnf install -y akmod-v4l2loopback v4l2loopback kernel-devel-$(uname -r)"}});
            p.steps.push_back({"build-module", "Build the driver for the running kernel",
                               {"akmods --force --kernels $(uname -r)"}});
            break;
        case Family::FedoraAtomic:
            p.steps.push_back({"install-fedora-atomic", "Layer RPM Fusion and the driver package (rpm-ostree)",
                               {std::string("rpm-ostree install -y --idempotent --apply-live ") + kRpmFusionUrl,
                                "rpm-ostree install -y --idempotent akmod-v4l2loopback v4l2loopback"}});
            p.restartAfter = true;
            p.notes.push_back("Fedora Atomic installs packages into a new system image. Restart when the setup "
                              "finishes, then open Bad Connection Studio again to load the driver.");
            break;
        case Family::Arch: {
            const std::string headers = (k.pkgbase.empty() ? std::string("linux") : k.pkgbase) + "-headers";
            p.steps.push_back({"install-arch", "Install the driver (DKMS) and kernel headers",
                               {"pacman -S --needed --noconfirm v4l2loopback-dkms " + headers}});
            p.steps.push_back({"build-module", "Build the driver for the running kernel",
                               {"dkms autoinstall -k $(uname -r)"}});
            p.notes.push_back("If pacman cannot download the packages, your package database is out of date: "
                              "run 'sudo pacman -Syu', restart, and try again.");
            break;
        }
        case Family::Debian:
            p.steps.push_back({"install-debian", "Install the driver (DKMS) and kernel headers",
                               {"apt-get update",
                                "apt-get install -y v4l2loopback-dkms linux-headers-$(uname -r)"}});
            p.steps.push_back({"build-module", "Build the driver for the running kernel",
                               {"dkms autoinstall -k $(uname -r)"}});
            break;
        case Family::OpenSuse:
            p.steps.push_back({"install-opensuse", "Install the driver package",
                               {"zypper --non-interactive install v4l2loopback-kmp-default"}});
            break;
        case Family::SteamOS:
        case Family::Unknown:
            break;
        }
        if (!p.restartAfter)
            p.steps.push_back({"load-module", "Load the driver", {load}});
        if (!k.headersPresent && d.family != Family::OpenSuse)
            p.notes.push_back("The headers of your running kernel (" + k.running + ") are missing and will be "
                              "installed. If your distribution no longer offers them, update your system, "
                              "restart, and try again.");
    }

    if (s == State::KernelOutdated)
        p.notes.insert(p.notes.begin(), describeState(s, d, k));

    if (persist && p.runnable)
        p.steps.push_back({"persist", "Load the driver automatically at every start",
                           {"echo v4l2loopback > /etc/modules-load.d/bad-connection-studio.conf",
                            std::string("echo 'options v4l2loopback ") + moduleOptions() +
                                "' > /etc/modprobe.d/bad-connection-studio.conf"}});

    if (k.secureBoot && s != State::Ready && s != State::InstalledNotLoaded) {
        switch (d.family) {
        case Family::Fedora:
        case Family::FedoraAtomic:
            p.notes.push_back("Secure Boot is on. akmods signs the driver with a key it creates; enroll that key "
                              "once with 'sudo mokutil --import /etc/pki/akmods/certs/public_key.der', choose a "
                              "one-time password, restart, and confirm it on the blue \"MOK management\" screen.");
            break;
        case Family::Debian:
            p.notes.push_back("Secure Boot is on. DKMS signs the driver with its own key; enroll it once with "
                              "'sudo mokutil --import /var/lib/dkms/mok.pub', restart, and confirm it on the "
                              "blue \"MOK management\" screen.");
            break;
        default:
            p.notes.push_back("Secure Boot is on. Unsigned drivers are refused (\"Key was rejected by service\"). "
                              "Sign the module with an enrolled key or see your distribution's Secure Boot "
                              "documentation.");
        }
    }
    return p;
}

std::string planReport(const DistroInfo& d, const KernelInfo& k, State s, const InstallPlan& p)
{
    std::ostringstream o;
    o << "distro:        " << d.prettyName << " (id=" << d.id << ", family=" << familyName(d.family) << ")\n"
      << "kernel:        " << k.running << (k.pkgbase.empty() ? "" : " (" + k.pkgbase + ")") << "\n"
      << "newest same:   " << (k.newestSameFlavor.empty() ? "-" : k.newestSameFlavor) << "\n"
      << "headers:       " << (k.headersPresent ? "yes" : "no") << "\n"
      << "module file:   " << (k.moduleFilePresent ? "yes" : "no") << "\n"
      << "source:        " << (k.sourcePresent ? "yes" : "no") << "\n"
      << "secure boot:   " << (k.secureBoot ? "on" : "off") << "\n"
      << "state:         " << stateName(s) << "\n"
      << "runnable:      " << (p.runnable ? "yes" : "no") << (p.restartAfter ? " (restart afterwards)" : "")
      << "\n";
    if (!p.blocker.empty())
        o << "blocker:       " << p.blocker << "\n";
    for (const auto& st : p.steps) {
        o << "step " << st.action << ": " << st.title << "\n";
        for (const auto& c : st.commands)
            o << "    # " << c << "\n";
    }
    for (const auto& n : p.notes)
        o << "note: " << n << "\n";
    return o.str();
}

} // namespace bcs::loopback
