#!/usr/bin/env bash
# PRE Buddy — single-device provisioner.
#
# Run this against a freshly-assembled CoreS3 to flash + provision + smoke
# test the device, so building 10 robots looks like 10 invocations of
# this script instead of 10 manual `idf.py` runs.
#
# Status: skeleton. Most stages are TODO until the first unit has been
# brought up by hand following docs/hardware-bringup.md. As each stage
# proves out on real silicon, fill it in here and the next unit becomes
# one command faster.
#
# Usage:
#   ./tools/provision_device.sh [--port /dev/cu.usbmodemXXX] [--label "Sage-001"]
#
# Exit codes:
#   0  device flashed, smoke-tested, labelled
#   1  generic failure
#   2  device not found on USB
#   3  smoke test failed

set -euo pipefail

# ── argument parsing ──────────────────────────────────────────────────

PORT=""
LABEL=""
SKIP_BUILD=0
SKIP_SMOKE=0

usage() {
    grep -E '^# ' "$0" | sed -E 's/^# ?//' | head -30
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)        PORT="$2"; shift 2 ;;
        --label)       LABEL="$2"; shift 2 ;;
        --skip-build)  SKIP_BUILD=1; shift ;;
        --skip-smoke)  SKIP_SMOKE=1; shift ;;
        -h|--help)     usage ;;
        *) echo "unknown arg: $1" >&2; exit 1 ;;
    esac
done

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

log() { printf '\033[36m[provision]\033[0m %s\n' "$*"; }
fail() { printf '\033[31m[provision] FAIL:\033[0m %s\n' "$*" >&2; exit "${2:-1}"; }

# ── stage 1: detect device on USB ─────────────────────────────────────

if [[ -z "$PORT" ]]; then
    matches=( /dev/cu.usbmodem* )
    if [[ ${#matches[@]} -eq 0 || "${matches[0]}" == "/dev/cu.usbmodem*" ]]; then
        fail "no CoreS3 found on USB. Plug one in, or pass --port." 2
    fi
    if [[ ${#matches[@]} -gt 1 ]]; then
        log "multiple devices found:"
        printf '  %s\n' "${matches[@]}"
        fail "ambiguous — pass --port explicitly." 2
    fi
    PORT="${matches[0]}"
fi
log "using device: $PORT"

# ── stage 2: ensure ESP-IDF is on PATH ─────────────────────────────────

if ! command -v idf.py >/dev/null 2>&1; then
    if [[ -f "$HOME/esp/esp-idf/export.sh" ]]; then
        log "sourcing ESP-IDF env"
        # shellcheck disable=SC1091
        . "$HOME/esp/esp-idf/export.sh" >/dev/null
    else
        fail "ESP-IDF not on PATH and not at ~/esp/esp-idf. Run tools/install_idf.sh first."
    fi
fi
idf.py --version | head -1

# ── stage 3: build (host) ─────────────────────────────────────────────

if [[ "$SKIP_BUILD" -eq 0 ]]; then
    log "building firmware/esp32 ..."
    pushd firmware/esp32 >/dev/null
    idf.py set-target esp32s3
    idf.py build
    popd >/dev/null
else
    log "skipping build (--skip-build)"
fi

# ── stage 4: flash ────────────────────────────────────────────────────

log "flashing device at $PORT ..."
pushd firmware/esp32 >/dev/null
idf.py -p "$PORT" flash
popd >/dev/null

# ── stage 5: read device MAC for labelling ────────────────────────────

# TODO: capture the device's BLE MAC from boot log so the label printer
# can print "Sage-001  MAC AA:BB:CC:DD:EE:FF  build-abc1234".
DEVICE_MAC="(MAC capture TODO — needs `idf.py -p $PORT monitor` parse)"
BUILD_HASH="$(git -C "$REPO_ROOT" rev-parse --short HEAD)"

# ── stage 6: provision default NVS config ─────────────────────────────

# TODO: once Esp32NvsCharacterStore.init() lands, write a default-empty
# NVS so the first boot lands on the character picker. Two options:
#   a) ship the device unprogrammed in NVS — picker runs every first boot.
#   b) use `parttool.py` to inject an empty "pre_buddy" namespace.
# Path (a) is what we'd want for the assembly line; this whole stage may
# be a no-op for that reason.
log "skipping NVS provisioning (handled by first-boot picker)"

# ── stage 7: smoke test sequence ─────────────────────────────────────

if [[ "$SKIP_SMOKE" -eq 0 ]]; then
    # TODO: run a host-side smoke harness that:
    #   1. Connects to the device's NUS as a BLE central
    #   2. Sends:
    #        pre.character.set → Sage
    #        pre.bg_agents.change → expect green LED
    #        pre.router.decision → expect purple LED + head nod
    #        pre.system.error → expect red LED, no motion
    #        pre.system.proximity → expect head-up
    #   3. Records actuator outcomes if instrumented (the kit doesn't
    #      have current sensors; for v1, visual inspection)
    log "TODO: smoke test sequence (not yet wired)"
    log "  manual check:"
    log "  1) device LED cycles through demo events:"
    log "       pre-buddy serve --transport ble --device-name pre-buddy --demo"
    log "  2) say 'hey buddy' → device acknowledges"
else
    log "skipping smoke test (--skip-smoke)"
fi

# ── stage 8: label ────────────────────────────────────────────────────

if [[ -n "$LABEL" ]]; then
    log "device labelled: $LABEL ($DEVICE_MAC, build $BUILD_HASH)"
    # TODO: integrate with a Brother QL or DYMO label printer via
    # `lp -d <printer> -o ...` or vendor-specific CLI.
else
    log "no label printed (pass --label <name> to enable)"
fi

log "✓ provisioning complete"
