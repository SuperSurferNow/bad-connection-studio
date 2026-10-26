#pragma once

#include <string>
#include <vector>

// Detection and install planning for the v4l2loopback kernel module.
//
// Everything here is read-only and unprivileged. The privileged part is a
// fixed helper script (packaging/linux/bcs-loopback-helper.sh) that accepts
// only the action names from allowedActions(); the GUI never passes free-form
// text to it. Every function takes a filesystem root so tests can run against
// fixture trees.
namespace bcs::loopback {

enum class Family {
    Fedora,
    FedoraAtomic, // Silverblue, Kinoite, Bazzite, Bluefin, ... (/run/ostree-booted)
    Arch,
    Debian,       // Debian, Ubuntu, Mint, Pop!_OS, ...
    OpenSuse,
    SteamOS,      // Arch-based but read-only root: instructions only
    Unknown,
};

struct DistroInfo {
    std::string id;
    std::string idLike;
    std::string versionId;
    std::string variantId;
    std::string prettyName;
    Family family = Family::Unknown;
};

DistroInfo parseOsRelease(const std::string& text, bool ostreeBooted);
DistroInfo detectDistro(const std::string& root = "/");
const char* familyName(Family f);

struct KernelInfo {
    std::string running;                // uname -r
    std::vector<std::string> installed; // kernels with an image, not leftover module dirs
    std::string newestSameFlavor;       // newest installed kernel of the running kernel's flavor
    std::string pkgbase;                // Arch: linux, linux-lts, linux-zen, ...
    bool modulesDirPresent = false;     // /usr/lib/modules/$(uname -r)/kernel exists
    bool headersPresent = false;        // /usr/lib/modules/$(uname -r)/build exists
    bool moduleFilePresent = false;     // v4l2loopback.ko* built for the running kernel
    bool sourcePresent = false;         // akmod / dkms source installed (package present, module not built)
    bool secureBoot = false;
};

// <0, 0, >0 like strcmp; compares the numeric runs ("6.9" < "6.10").
int compareKernelVersions(const std::string& a, const std::string& b);
// Shape of a release string with digits removed, e.g. "#.#.#-#-lts". Kernels
// with the same shape are the same flavor (so linux-lts never "outdates" linux).
std::string kernelFlavor(const std::string& release);
KernelInfo detectKernel(const std::string& root = "/", const std::string& running = {});

enum class State {
    Ready,              // module loaded
    InstalledNotLoaded, // module built for this kernel: only modprobe needed
    NotBuilt,           // package installed, module not built for the running kernel
    NotInstalled,
    KernelOutdated,     // a newer kernel of this flavor is installed: restart recommended
    NeedsReboot,        // running kernel's modules were removed by an update: restart required
    Unsupported,        // unknown distro or read-only system without the module
};

State evaluate(bool moduleLoaded, const DistroInfo& d, const KernelInfo& k);
const char* stateName(State s);
std::string describeState(State s, const DistroInfo& d, const KernelInfo& k);

struct Step {
    std::string action;                // one of allowedActions()
    std::string title;                 // human-readable
    std::vector<std::string> commands; // shown in the preview; the helper runs the equivalent
};

struct InstallPlan {
    bool runnable = false;       // false: show instructions only
    bool restartAfter = false;   // Fedora Atomic layering needs a restart
    std::string blocker;         // why it is not runnable / what to do first
    std::vector<Step> steps;
    std::vector<std::string> notes;
    std::vector<std::string> actions() const;
};

const std::vector<std::string>& allowedActions();
bool isAllowedAction(const std::string& a);

InstallPlan buildPlan(const DistroInfo& d, const KernelInfo& k, State s, bool persist);

// Module options used everywhere (modprobe and /etc/modprobe.d).
const char* moduleOptions();

// Human-readable dump for --loopback-plan.
std::string planReport(const DistroInfo& d, const KernelInfo& k, State s, const InstallPlan& p);

} // namespace bcs::loopback
