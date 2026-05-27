#!/usr/bin/env bash
# PRE Buddy — ESP-IDF v5.x bootstrap.
#
# One-time install of the Espressif toolchain on a fresh dev machine.
# Wraps the official Espressif install script + a few sanity checks.
#
# Default install location: $HOME/esp/esp-idf
# Default version:         v5.3.1 (latest stable as of bring-up)
#
# After install, source the env into any shell with:
#     . $HOME/esp/esp-idf/export.sh
#
# Or add to ~/.zshrc:
#     alias get_idf='. $HOME/esp/esp-idf/export.sh'

set -euo pipefail

IDF_VERSION="${IDF_VERSION:-v5.3.1}"
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"

log() { printf '\033[36m[install-idf]\033[0m %s\n' "$*"; }
fail() { printf '\033[31m[install-idf] FAIL:\033[0m %s\n' "$*" >&2; exit 1; }

# ── prereqs ───────────────────────────────────────────────────────────

case "$(uname -s)" in
    Darwin) ;;
    Linux)  ;;
    *) fail "unsupported OS: $(uname -s)" ;;
esac

for tool in git cmake ninja; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        fail "$tool not found. Install via Homebrew: brew install $tool"
    fi
done

# ESP-IDF v5.3 formally supports Python 3.9–3.12. Use the newest version
# in that window that's actually installed; export ESP_PYTHON so the
# Espressif installer uses it instead of whatever `python3` defaults to.
ESP_PYTHON=""
for candidate in python3.12 python3.11 python3.10 python3.9 python3; do
    if command -v "$candidate" >/dev/null 2>&1; then
        ver=$($candidate -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')
        case "$ver" in
            3.9|3.10|3.11|3.12)
                ESP_PYTHON="$(command -v "$candidate")"
                break ;;
        esac
    fi
done
if [[ -z "$ESP_PYTHON" ]]; then
    fail "no Python 3.9-3.12 found. Install one: brew install python@3.12"
fi
export ESP_PYTHON
log "using Python: $ESP_PYTHON ($($ESP_PYTHON --version))"

# ── clone ESP-IDF ─────────────────────────────────────────────────────
#
# ESP-IDF + submodules is ~1.5 GB. The default git config can timeout on
# slower / less stable networks while the server is still sending body
# data. We bump postBuffer, force HTTP/2 to be off (some MITM proxies
# choke on it), do a shallow clone, and retry on transient failure.

git_safe_clone() {
    local attempts=0
    local max_attempts=3
    while (( attempts < max_attempts )); do
        attempts=$((attempts + 1))
        log "clone attempt $attempts/$max_attempts ..."
        if git -c http.postBuffer=524288000 \
               -c http.version=HTTP/1.1 \
               -c http.lowSpeedLimit=1000 \
               -c http.lowSpeedTime=600 \
               clone --branch "$IDF_VERSION" \
                     --depth 1 \
                     --shallow-submodules \
                     --recurse-submodules \
                     https://github.com/espressif/esp-idf.git "$IDF_PATH"; then
            return 0
        fi
        log "clone failed — cleaning up and retrying in 5s"
        rm -rf "$IDF_PATH"
        sleep 5
    done
    return 1
}

if [[ -d "$IDF_PATH" && -d "$IDF_PATH/.git" ]]; then
    log "ESP-IDF already at $IDF_PATH — fetching tags + checking out $IDF_VERSION"
    git -C "$IDF_PATH" fetch --tags
    git -C "$IDF_PATH" checkout "$IDF_VERSION"
    git -C "$IDF_PATH" submodule update --init --recursive --depth 1
else
    log "cloning ESP-IDF $IDF_VERSION to $IDF_PATH (shallow, ~1.5 GB)"
    mkdir -p "$(dirname "$IDF_PATH")"
    rm -rf "$IDF_PATH"   # in case a previous partial clone left an empty dir
    if ! git_safe_clone; then
        fail "ESP-IDF clone failed after 3 attempts — check network / VPN / proxy"
    fi
fi

# ── run Espressif's installer ─────────────────────────────────────────

log "installing toolchains for esp32s3 (this downloads ~2-3 GB)"
cd "$IDF_PATH"
./install.sh esp32s3

# ── verify ────────────────────────────────────────────────────────────

# shellcheck disable=SC1091
. "$IDF_PATH/export.sh" >/dev/null

if ! command -v idf.py >/dev/null 2>&1; then
    fail "idf.py still not on PATH after export.sh"
fi

log "✓ ESP-IDF $IDF_VERSION installed at $IDF_PATH"
log "  idf.py: $(command -v idf.py)"
log ""
log "Add this to ~/.zshrc for convenience:"
log "    alias get_idf='. $IDF_PATH/export.sh'"
log ""
log "Then in any shell where you want to build firmware:  get_idf"
