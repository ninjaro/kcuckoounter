#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

ANDROID_ENV_SILENT=1
source "$SCRIPT_DIR/env.sh"

build_dir="${ANDROID_BUILD_DIR:-$ROOT_DIR/build-android-debug}"

if [ ! -d "$build_dir" ]; then
  die "build dir $build_dir not found; run ./scripts/cli.sh build android first"
fi

cmake --build "$build_dir" --target apk --parallel "$(nproc_safe)"

adb_bin="$(android_adb_path)" || die "adb not found; set ANDROID_SDK_ROOT or ADB_BIN."

apk_path="$(android_find_apk "$build_dir")" || die "could not locate debug apk under $build_dir"
package_name="$(android_apk_package_name "$apk_path")" || die "unable to derive package name from $apk_path (aapt required)"

boot_timeout="${ANDROID_EMULATOR_BOOT_TIMEOUT:-300}"
log_file="${ANDROID_EMULATOR_LOG:-/tmp/emulator-${ANDROID_AVD_NAME}.log}"

serial="$(android_first_emulator_serial "$adb_bin")"
if [ -z "$serial" ]; then
  if [ ! -x "$ANDROID_EMULATOR_BIN" ]; then
    die "emulator binary '$ANDROID_EMULATOR_BIN' not found; set ANDROID_EMULATOR_BIN or start an emulator."
  fi

  log "no emulator detected; starting '$ANDROID_AVD_NAME' (log: $log_file)..."
  "$ANDROID_EMULATOR_BIN" -avd "$ANDROID_AVD_NAME" \
    -netdelay none -netspeed full -gpu host \
    -no-boot-anim -no-snapshot \
    >"$log_file" 2>&1 &

  serial=""
  start_time="$(date +%s)"
  while [ -z "$serial" ]; do
    sleep 1
    if [ $(( $(date +%s) - start_time )) -ge "$boot_timeout" ]; then
      log "emulator failed to boot within ${boot_timeout}s; see $log_file for details."
      if [ -f "$log_file" ]; then
        tail -n 200 "$log_file"
      fi
      exit 1
    fi
    serial="$(android_first_emulator_serial "$adb_bin")"
  done
fi

if ! android_wait_for_boot "$adb_bin" "$serial" "$boot_timeout"; then
  log "emulator '$serial' is not fully booted; check diagnostics."
  if [ -f "$log_file" ]; then
    tail -n 200 "$log_file"
  fi
  exit 1
fi
log "emulator ready: $serial"

"$adb_bin" -s "$serial" install -r "$apk_path"
"$adb_bin" -s "$serial" shell monkey -p "$package_name" -c android.intent.category.LAUNCHER 1
