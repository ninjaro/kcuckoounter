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

serial="$(android_first_device_serial "$adb_bin")"
if [ -z "$serial" ]; then
  die "no physical device detected; connect a device with USB debugging enabled."
fi

"$adb_bin" -s "$serial" install -r "$apk_path"
"$adb_bin" -s "$serial" shell monkey -p "$package_name" -c android.intent.category.LAUNCHER 1
