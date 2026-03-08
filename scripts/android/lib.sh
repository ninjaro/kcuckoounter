#!/usr/bin/env bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib/common.sh"

detect_android_sdk_root() {
  if [ -n "${ANDROID_SDK_ROOT:-}" ] && [ -d "$ANDROID_SDK_ROOT" ]; then
    echo "$ANDROID_SDK_ROOT"
    return 0
  fi

  for dir in "$HOME/Android/Sdk" "/opt/android-sdk"; do
    if [ -d "$dir" ]; then
      echo "$dir"
      return 0
    fi
  done

  if command -v adb >/dev/null 2>&1; then
    local adb_path sdk_from_adb
    adb_path="$(command -v adb)"
    sdk_from_adb="$(cd "$(dirname "$adb_path")/.." 2>/dev/null && pwd)"
    if [ -n "$sdk_from_adb" ]; then
      echo "$sdk_from_adb"
      return 0
    fi
  fi

  if command -v emulator >/dev/null 2>&1; then
    local emu_path sdk_from_emu
    emu_path="$(command -v emulator)"
    sdk_from_emu="$(cd "$(dirname "$emu_path")/.." 2>/dev/null && pwd)"
    if [ -n "$sdk_from_emu" ]; then
      echo "$sdk_from_emu"
      return 0
    fi
  fi

  return 1
}

detect_android_emulator() {
  if [ -n "${ANDROID_EMULATOR_BIN:-}" ] && [ -x "$ANDROID_EMULATOR_BIN" ]; then
    echo "$ANDROID_EMULATOR_BIN"
    return 0
  fi

  if [ -n "${ANDROID_SDK_ROOT:-}" ] && [ -x "$ANDROID_SDK_ROOT/emulator/emulator" ]; then
    echo "$ANDROID_SDK_ROOT/emulator/emulator"
    return 0
  fi

  if command -v emulator >/dev/null 2>&1; then
    command -v emulator
    return 0
  fi

  return 1
}

android_adb_path() {
  local sdk_root adb_bin
  sdk_root="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
  adb_bin="${ADB_BIN:-$sdk_root/platform-tools/adb}"

  if [ -x "$adb_bin" ]; then
    echo "$adb_bin"
    return 0
  fi

  if command -v adb >/dev/null 2>&1; then
    command -v adb
    return 0
  fi

  return 1
}

android_find_apk() {
  local build_dir preferred_abi apk_path
  local -a apk_candidates
  local -a abi_candidates
  build_dir="$1"
  preferred_abi="${ANDROID_ABI:-}"

  mapfile -t apk_candidates < <(
    find "$build_dir" -type f -path '*outputs/apk/*/*-debug.apk' | sort
  )
  if [ "${#apk_candidates[@]}" -eq 0 ]; then
    return 1
  fi

  if [ -n "$preferred_abi" ]; then
    for apk_path in "${apk_candidates[@]}"; do
      if [[ "$apk_path" == *"$preferred_abi"* ]]; then
        abi_candidates+=("$apk_path")
      fi
    done
  fi

  if [ "${#abi_candidates[@]}" -gt 0 ]; then
    echo "${abi_candidates[0]}"
    return 0
  fi

  if [ -n "${ANDROID_APK_PATH_HINT:-}" ]; then
    for apk_path in "${apk_candidates[@]}"; do
      if [[ "$apk_path" == *"${ANDROID_APK_PATH_HINT}"* ]]; then
        echo "$apk_path"
        return 0
      fi
    done
  fi

  if [ -n "${apk_candidates[0]:-}" ]; then
    echo "${apk_candidates[0]}"
    return 0
  fi
  return 1
}

android_aapt_path() {
  local sdk_root build_tools_version aapt_bin
  sdk_root="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
  build_tools_version="${ANDROID_BUILD_TOOLS_VERSION:-}"

  if [ -n "${AAPT_BIN:-}" ] && [ -x "${AAPT_BIN:-}" ]; then
    echo "$AAPT_BIN"
    return 0
  fi

  if [ -n "$build_tools_version" ]; then
    aapt_bin="$sdk_root/build-tools/$build_tools_version/aapt"
    if [ -x "$aapt_bin" ]; then
      echo "$aapt_bin"
      return 0
    fi
  fi

  if [ -d "$sdk_root/build-tools" ]; then
    aapt_bin="$(find "$sdk_root/build-tools" -type f -name aapt | sort | tail -n 1 || true)"
    if [ -n "$aapt_bin" ] && [ -x "$aapt_bin" ]; then
      echo "$aapt_bin"
      return 0
    fi
  fi

  if command -v aapt >/dev/null 2>&1; then
    command -v aapt
    return 0
  fi

  return 1
}

android_apk_package_name() {
  local apk_path="$1"
  local aapt_bin badging
  aapt_bin="$(android_aapt_path)" || return 1
  badging="$("$aapt_bin" dump badging "$apk_path" 2>/dev/null)" || return 1
  echo "$badging" | awk -F"'" '/^package: name=/{print $2; exit}'
}

android_first_emulator_serial() {
  local adb_bin="$1"
  "$adb_bin" devices -l | awk 'NR>1 && $1 ~ /^emulator-/ {print $1; exit}'
}

android_first_device_serial() {
  local adb_bin="$1"
  "$adb_bin" devices -l | awk 'NR>1 && $1 !~ /^emulator-/ {print $1; exit}'
}

android_wait_for_boot() {
  local adb_bin="$1"
  local serial="$2"
  local timeout_seconds="${3:-300}"
  local started_at now elapsed boot_completed

  "$adb_bin" -s "$serial" wait-for-device
  started_at="$(date +%s)"
  while true; do
    boot_completed="$("$adb_bin" -s "$serial" shell getprop sys.boot_completed | tr -d '\r')"
    if [ "$boot_completed" = "1" ]; then
      return 0
    fi

    now="$(date +%s)"
    elapsed=$(( now - started_at ))
    if [ "$elapsed" -ge "$timeout_seconds" ]; then
      log "device '$serial' did not report boot completion within ${timeout_seconds}s"
      "$adb_bin" -s "$serial" shell getprop sys.boot_completed || true
      "$adb_bin" -s "$serial" shell getprop init.svc.bootanim || true
      return 1
    fi
    sleep 1
  done
}
