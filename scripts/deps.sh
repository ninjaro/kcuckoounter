#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"

mode="${1:-desktop}"

case "$mode" in
  desktop|android|all) ;;
  *)
    die "usage: $0 {desktop|android|all}"
    ;;
esac

declare -a missing=()

check_desktop_deps() {
  local required_tools=(rg clang-format cmake ctest)
  for tool in "${required_tools[@]}"; do
    if ! command_exists "$tool"; then
      missing+=("$tool")
    fi
  done
}

check_android_deps() {
  local required_tools=(adb)
  for tool in "${required_tools[@]}"; do
    if ! command_exists "$tool"; then
      missing+=("$tool")
    fi
  done

  if ! command_exists emulator && [[ ! -x "${ANDROID_SDK_ROOT:-}/emulator/emulator" ]]; then
    missing+=("emulator")
  fi

  if ! command_exists aapt; then
    local aapt_found=""
    if [[ -n "${ANDROID_SDK_ROOT:-}" && -d "${ANDROID_SDK_ROOT}/build-tools" ]]; then
      aapt_found="$(find "${ANDROID_SDK_ROOT}/build-tools" -type f -name aapt | head -n 1 || true)"
    fi
    if [[ -z "$aapt_found" ]]; then
      missing+=("aapt (Android build-tools)")
    fi
  fi

  if [[ -x "$SCRIPT_DIR/android/deps.sh" ]]; then
    if ! "$SCRIPT_DIR/android/deps.sh" build >/dev/null 2>&1; then
      missing+=("android sdk components (run ./scripts/cli.sh android deps build)")
    fi
  fi
}

if [[ "$mode" == "desktop" || "$mode" == "all" ]]; then
  check_desktop_deps
fi

if [[ "$mode" == "android" || "$mode" == "all" ]]; then
  check_android_deps
fi

if [[ "${#missing[@]}" -ne 0 ]]; then
  log "Missing required tools for mode '$mode':"
  for tool in "${missing[@]}"; do
    log "  - $tool"
  done
  exit 1
fi

log "All required tools are available for mode '$mode'."
