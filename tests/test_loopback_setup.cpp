#include "backend/linux/LoopbackSetup.h"
#include "check.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

using namespace bcs::loopback;
namespace fs = std::filesystem;

namespace {

// Fixture filesystem root under /tmp, removed at exit.
struct Root {
    fs::path dir;
    Root()
    {
        dir = fs::temp_directory_path() / ("bcs-loopback-test-" + std::to_string(::getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Root() { fs::remove_all(dir); }
    void file(const std::string& rel, const std::string& content = "x")
    {
        fs::path p = dir / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << content;
    }
    void kernel(const std::string& rel, bool image = true)
    {
        fs::create_directories(dir / "usr/lib/modules" / rel / "kernel");
        if (image)
            file("usr/lib/modules/" + rel + "/vmlinuz");
    }
    std::string str() const { return dir.string(); }
};

bool hasAction(const InstallPlan& p, const std::string& a)
{
    auto v = p.actions();
    return std::find(v.begin(), v.end(), a) != v.end();
}

bool commandsMention(const InstallPlan& p, const std::string& needle)
{
    for (const auto& s : p.steps)
        for (const auto& c : s.commands)
            if (c.find(needle) != std::string::npos)
                return true;
    return false;
}

} // namespace

int main()
{
    // ---- os-release parsing
    CHECK(parseOsRelease("ID=fedora\nVERSION_ID=44\n", false).family == Family::Fedora);
    CHECK(parseOsRelease("ID=fedora\nVARIANT_ID=silverblue\n", true).family == Family::FedoraAtomic);
    CHECK(parseOsRelease("ID=bazzite\nID_LIKE=\"fedora\"\n", true).family == Family::FedoraAtomic);
    CHECK(parseOsRelease("ID=arch\n", false).family == Family::Arch);
    CHECK(parseOsRelease("ID=endeavouros\nID_LIKE=arch\n", false).family == Family::Arch);
    CHECK(parseOsRelease("ID=manjaro\nID_LIKE=arch\n", false).family == Family::Arch);
    CHECK(parseOsRelease("ID=steamos\nID_LIKE=arch\n", false).family == Family::SteamOS);
    CHECK(parseOsRelease("ID=ubuntu\nID_LIKE=debian\n", false).family == Family::Debian);
    CHECK(parseOsRelease("ID=linuxmint\nID_LIKE=\"ubuntu debian\"\n", false).family == Family::Debian);
    CHECK(parseOsRelease("ID=pop\nID_LIKE=\"ubuntu debian\"\n", false).family == Family::Debian);
    CHECK(parseOsRelease("ID=\"opensuse-tumbleweed\"\nID_LIKE=\"opensuse suse\"\n", false).family ==
          Family::OpenSuse);
    CHECK(parseOsRelease("ID=gentoo\n", false).family == Family::Unknown);
    CHECK(parseOsRelease("ID=fedora\nPRETTY_NAME=\"Fedora Linux 44\"\n", false).prettyName == "Fedora Linux 44");

    // ---- kernel versions and flavors
    CHECK(compareKernelVersions("6.9.1-arch1-1", "6.10.0-arch1-1") < 0);
    CHECK(compareKernelVersions("7.2.5-200.fc44.x86_64", "7.2.4-200.fc44.x86_64") > 0);
    CHECK(compareKernelVersions("6.8.0-45-generic", "6.8.0-45-generic") == 0);
    CHECK(kernelFlavor("6.8.0-45-generic") == kernelFlavor("6.8.0-50-generic"));
    CHECK(kernelFlavor("6.8.0-45-generic") != kernelFlavor("6.8.0-45-lowlatency"));
    CHECK(kernelFlavor("6.12.48+deb13-amd64") != kernelFlavor("6.12.48+deb13-rt-amd64"));

    // ---- Fedora, up to date, nothing installed (leftover module dirs ignored)
    {
        Root r;
        r.file("etc/os-release", "ID=fedora\nVERSION_ID=44\n");
        r.kernel("7.2.5-200.fc44.x86_64");
        r.kernel("7.3.0-100.fc44.x86_64", /*image=*/false); // leftover dir, no vmlinuz
        fs::create_directories(r.dir / "usr/lib/modules/7.2.5-200.fc44.x86_64/build");
        auto d = detectDistro(r.str());
        auto k = detectKernel(r.str(), "7.2.5-200.fc44.x86_64");
        CHECK(k.installed.size() == 1);
        CHECK(k.headersPresent);
        auto s = evaluate(false, d, k);
        CHECK(s == State::NotInstalled);
        auto p = buildPlan(d, k, s, true);
        CHECK(p.runnable);
        CHECK(hasAction(p, "enable-rpmfusion") && hasAction(p, "install-fedora") && hasAction(p, "build-module"));
        CHECK(hasAction(p, "load-module") && hasAction(p, "persist"));
        CHECK(commandsMention(p, "kernel-devel-$(uname -r)"));
        CHECK(!buildPlan(d, k, s, false).actions().empty() && !hasAction(buildPlan(d, k, s, false), "persist"));
        CHECK(evaluate(true, d, k) == State::Ready);
    }

    // ---- Fedora: newer kernel installed, not rebooted -> restart recommended, still runnable
    {
        Root r;
        r.file("etc/os-release", "ID=fedora\n");
        r.kernel("7.2.4-200.fc44.x86_64");
        r.kernel("7.2.5-200.fc44.x86_64");
        auto d = detectDistro(r.str());
        auto k = detectKernel(r.str(), "7.2.4-200.fc44.x86_64");
        CHECK(k.newestSameFlavor == "7.2.5-200.fc44.x86_64");
        auto s = evaluate(false, d, k);
        CHECK(s == State::KernelOutdated);
        auto p = buildPlan(d, k, s, false);
        CHECK(p.runnable);
        CHECK(!p.notes.empty() && p.notes.front().find("Restart") != std::string::npos);
        CHECK(!k.headersPresent);
    }

    // ---- Module already built for the running kernel -> just modprobe
    {
        Root r;
        r.file("etc/os-release", "ID=fedora\n");
        r.kernel("7.2.5-200.fc44.x86_64");
        r.file("usr/lib/modules/7.2.5-200.fc44.x86_64/extra/v4l2loopback/v4l2loopback.ko.xz");
        auto d = detectDistro(r.str());
        auto k = detectKernel(r.str(), "7.2.5-200.fc44.x86_64");
        CHECK(k.moduleFilePresent);
        auto s = evaluate(false, d, k);
        CHECK(s == State::InstalledNotLoaded);
        auto p = buildPlan(d, k, s, false);
        CHECK(p.actions() == std::vector<std::string>{"load-module"});
    }

    // ---- Arch after pacman -Syu without reboot: running kernel's modules are gone
    {
        Root r;
        r.file("etc/os-release", "ID=arch\n");
        r.kernel("6.17.2-arch1-1");
        r.file("usr/lib/modules/6.17.2-arch1-1/pkgbase", "linux\n");
        auto d = detectDistro(r.str());
        auto k = detectKernel(r.str(), "6.17.1-arch1-1");
        CHECK(!k.modulesDirPresent);
        auto s = evaluate(false, d, k);
        CHECK(s == State::NeedsReboot);
        auto p = buildPlan(d, k, s, true);
        CHECK(!p.runnable && p.steps.empty() && !p.blocker.empty());
    }

    // ---- Arch LTS next to mainline: the newer mainline kernel does not "outdate" LTS
    {
        Root r;
        r.file("etc/os-release", "ID=arch\n");
        r.kernel("6.12.50-1-lts");
        r.file("usr/lib/modules/6.12.50-1-lts/pkgbase", "linux-lts\n");
        r.kernel("6.17.2-arch1-1");
        r.file("usr/lib/modules/6.17.2-arch1-1/pkgbase", "linux\n");
        r.file("usr/src/v4l2loopback-0.15.1/dkms.conf");
        auto d = detectDistro(r.str());
        auto k = detectKernel(r.str(), "6.12.50-1-lts");
        CHECK(k.pkgbase == "linux-lts");
        CHECK(k.newestSameFlavor == "6.12.50-1-lts");
        CHECK(k.sourcePresent);
        auto s = evaluate(false, d, k);
        CHECK(s == State::NotBuilt);
        auto p = buildPlan(d, k, s, false);
        CHECK(commandsMention(p, "linux-lts-headers"));
        CHECK(hasAction(p, "install-arch") && hasAction(p, "build-module"));
    }

    // ---- Debian with Secure Boot on
    {
        Root r;
        r.file("etc/os-release", "ID=debian\nVERSION_ID=13\n");
        r.file("boot/vmlinuz-6.12.48+deb13-amd64");
        fs::create_directories(r.dir / "usr/lib/modules/6.12.48+deb13-amd64/kernel");
        r.file("sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c",
               std::string("\x06\x00\x00\x00\x01", 5));
        auto d = detectDistro(r.str());
        auto k = detectKernel(r.str(), "6.12.48+deb13-amd64");
        CHECK(k.installed.size() == 1); // image found in /boot
        CHECK(k.secureBoot);
        auto s = evaluate(false, d, k);
        CHECK(s == State::NotInstalled);
        auto p = buildPlan(d, k, s, false);
        CHECK(hasAction(p, "install-debian"));
        CHECK(commandsMention(p, "linux-headers-$(uname -r)"));
        bool mok = false;
        for (const auto& n : p.notes)
            mok |= n.find("mokutil") != std::string::npos;
        CHECK(mok);
    }

    // ---- Fedora Atomic: layering, restart afterwards, no modprobe in the same run
    {
        Root r;
        r.file("etc/os-release", "ID=fedora\nVARIANT_ID=kinoite\n");
        r.file("run/ostree-booted", "");
        r.kernel("7.2.5-200.fc44.x86_64");
        auto d = detectDistro(r.str());
        CHECK(d.family == Family::FedoraAtomic);
        auto k = detectKernel(r.str(), "7.2.5-200.fc44.x86_64");
        auto p = buildPlan(d, k, evaluate(false, d, k), true);
        CHECK(p.restartAfter);
        CHECK(hasAction(p, "install-fedora-atomic") && !hasAction(p, "load-module"));
        CHECK(hasAction(p, "persist"));
    }

    // ---- Bazzite ships the module: only modprobe
    {
        Root r;
        r.file("etc/os-release", "ID=bazzite\nID_LIKE=\"fedora\"\n");
        r.file("run/ostree-booted", "");
        r.kernel("7.2.5-201.bazzite.fc44.x86_64");
        r.file("usr/lib/modules/7.2.5-201.bazzite.fc44.x86_64/extra/v4l2loopback.ko.xz");
        auto d = detectDistro(r.str());
        auto k = detectKernel(r.str(), "7.2.5-201.bazzite.fc44.x86_64");
        auto s = evaluate(false, d, k);
        CHECK(s == State::InstalledNotLoaded);
        CHECK(buildPlan(d, k, s, false).actions() == std::vector<std::string>{"load-module"});
    }

    // ---- SteamOS / unknown: instructions only
    {
        Root r;
        r.file("etc/os-release", "ID=steamos\nID_LIKE=arch\n");
        r.kernel("6.11.11-valve12-1-neptune-611");
        auto d = detectDistro(r.str());
        auto k = detectKernel(r.str(), "6.11.11-valve12-1-neptune-611");
        auto s = evaluate(false, d, k);
        CHECK(s == State::Unsupported);
        auto p = buildPlan(d, k, s, true);
        CHECK(!p.runnable && p.steps.empty() && !p.notes.empty());
    }

    // ---- openSUSE
    {
        DistroInfo d = parseOsRelease("ID=\"opensuse-tumbleweed\"\nID_LIKE=\"opensuse suse\"\n", false);
        KernelInfo k;
        k.running = "6.17.1-1-default";
        k.modulesDirPresent = true;
        auto p = buildPlan(d, k, evaluate(false, d, k), false);
        CHECK(hasAction(p, "install-opensuse") && hasAction(p, "load-module"));
    }

    // ---- every plan only uses allow-listed actions, and the helper script handles each of them
    {
        std::ifstream f(BCS_HELPER_SCRIPT);
        std::string script((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(!script.empty());
        for (const auto& a : allowedActions())
            CHECK(script.find("\n        " + a + ")") != std::string::npos);
        CHECK(!isAllowedAction("rm -rf /"));
        CHECK(!isAllowedAction("load-module; reboot"));
        for (Family fam : {Family::Fedora, Family::FedoraAtomic, Family::Arch, Family::Debian, Family::OpenSuse}) {
            DistroInfo d;
            d.family = fam;
            KernelInfo k;
            k.running = "6.17.1-1";
            k.modulesDirPresent = true;
            for (const auto& a : buildPlan(d, k, State::NotInstalled, true).actions())
                CHECK(isAllowedAction(a));
        }
    }

    return TEST_RESULT();
}
