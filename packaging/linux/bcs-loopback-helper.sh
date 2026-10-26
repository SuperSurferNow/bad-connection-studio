#!/bin/bash
# Bad Connection Studio: v4l2loopback setup helper. Runs as root.
#
# The app embeds this script and pipes it to `pkexec systemd-run ... bash -s`;
# it is never written to disk by the app. The only inputs are action names
# from a fixed list (see LoopbackSetup.cpp, allowedActions()); every other
# value (kernel release, distro, package names) is derived here.
#
# Exit codes: 0 done, 10 restart required before continuing, 2 bad action,
# anything else: a step failed.
#
# Everything lives in main() so bash parses the whole script before running
# anything: package managers inherit stdin and must not read script text.

main() {
    set -euo pipefail
    export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
    export LC_ALL=C
    umask 022
    exec </dev/null

    local KREL OPTIONS RPMFUSION_URL
    KREL="$(uname -r)"
    OPTIONS='devices=1 video_nr=10 card_label="Bad Connection Camera" exclusive_caps=1'
    RPMFUSION_URL="https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora 2>/dev/null || echo 0).noarch.rpm"

    step() { echo "BCS-STEP: $*"; }
    fail() { echo "BCS-FAIL: $*"; exit 1; }

    show_build_log() {
        local f
        for f in /var/lib/dkms/v4l2loopback/*/build/make.log /var/lib/dkms/v4l2loopback/*/log/make.log \
                 /var/cache/akmods/v4l2loopback/*"${KREL}"*.failed.log; do
            [ -r "$f" ] || continue
            echo "---- last lines of $f ----"
            tail -n 25 "$f"
        done
    }

    local a restart=0
    for a in "$@"; do
        case "$a" in
            enable-rpmfusion|install-fedora|install-fedora-atomic|install-arch|install-debian|install-opensuse|build-module|load-module|persist) ;;
            *) echo "BCS-FAIL: unknown action"; exit 2 ;;
        esac
    done

    for a in "$@"; do
        case "$a" in
        enable-rpmfusion)
            step "Enabling RPM Fusion free"
            if rpm -q rpmfusion-free-release >/dev/null 2>&1; then
                echo "already enabled"
            else
                dnf install -y "$RPMFUSION_URL" || fail "could not enable RPM Fusion"
            fi
            ;;
        install-fedora)
            step "Installing akmod-v4l2loopback and kernel-devel-${KREL}"
            dnf install -y akmod-v4l2loopback v4l2loopback "kernel-devel-${KREL}" \
                || fail "dnf could not install the packages (kernel-devel-${KREL} may no longer be available: update, restart and retry)"
            ;;
        install-fedora-atomic)
            step "Layering RPM Fusion and the driver with rpm-ostree"
            if ! rpm -q rpmfusion-free-release >/dev/null 2>&1; then
                rpm-ostree install -y --idempotent --apply-live "$RPMFUSION_URL" \
                    || fail "rpm-ostree could not add RPM Fusion"
            fi
            if ! ls /etc/yum.repos.d/rpmfusion-free*.repo >/dev/null 2>&1; then
                echo "BCS-RESTART: RPM Fusion is staged. Restart, then run the setup again."
                exit 10
            fi
            rpm-ostree install -y --idempotent akmod-v4l2loopback v4l2loopback \
                || fail "rpm-ostree could not install akmod-v4l2loopback"
            echo "BCS-RESTART: the driver is staged in a new system image. Restart to finish."
            restart=1
            ;;
        install-arch)
            local pkgbase="linux"
            [ -r "/usr/lib/modules/${KREL}/pkgbase" ] && pkgbase="$(cat "/usr/lib/modules/${KREL}/pkgbase")"
            case "$pkgbase" in *[!a-z0-9-]*|"") pkgbase="linux" ;; esac
            step "Installing v4l2loopback-dkms and ${pkgbase}-headers"
            pacman -S --needed --noconfirm v4l2loopback-dkms "${pkgbase}-headers" \
                || fail "pacman failed (if downloads 404, run 'pacman -Syu', restart and retry)"
            ;;
        install-debian)
            step "Installing v4l2loopback-dkms and linux-headers-${KREL}"
            export DEBIAN_FRONTEND=noninteractive
            apt-get update || fail "apt-get update failed"
            apt-get install -y v4l2loopback-dkms "linux-headers-${KREL}" \
                || fail "apt could not install the packages (linux-headers-${KREL} may no longer be available: update, restart and retry)"
            ;;
        install-opensuse)
            step "Installing v4l2loopback-kmp-default"
            zypper --non-interactive install v4l2loopback-kmp-default || fail "zypper failed"
            ;;
        build-module)
            step "Building the driver for kernel ${KREL}"
            [ -e "/usr/lib/modules/${KREL}/build" ] || fail "headers for kernel ${KREL} are not installed"
            if command -v akmods >/dev/null 2>&1; then
                akmods --force --kernels "$KREL" || { show_build_log; fail "the driver did not build for kernel ${KREL}"; }
            elif command -v dkms >/dev/null 2>&1; then
                dkms autoinstall -k "$KREL" || { show_build_log; fail "the driver did not build for kernel ${KREL}"; }
            else
                fail "neither akmods nor dkms is installed"
            fi
            ;;
        load-module)
            step "Loading v4l2loopback"
            if [ -d /sys/module/v4l2loopback ]; then
                echo "already loaded"
            else
                local out
                if ! out="$(eval "modprobe v4l2loopback $OPTIONS" 2>&1)"; then
                    echo "$out"
                    case "$out" in
                        *"Key was rejected"*) fail "Secure Boot refused the unsigned driver (enroll the signing key, see the notes)" ;;
                        *) fail "modprobe failed" ;;
                    esac
                fi
            fi
            ;;
        persist)
            step "Loading the driver at every start"
            echo v4l2loopback > /etc/modules-load.d/bad-connection-studio.conf
            echo "options v4l2loopback $OPTIONS" > /etc/modprobe.d/bad-connection-studio.conf
            ;;
        esac
    done
    [ "$restart" = 1 ] && exit 10
    echo "BCS-DONE"
}

main "$@"
