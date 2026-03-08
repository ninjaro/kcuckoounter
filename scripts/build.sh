#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"

mode="${1:-all}"
build_type="${BUILD_TYPE:-Debug}"
install_prefix="${INSTALL_PREFIX:-$HOME/kde/usr}"
parallel="${PARALLEL:-$(nproc_safe)}"

build_desktop() {
  local name="$1"
  local kde_flag="$2"
  local build_dir="$ROOT_DIR/build-$name"

  cmake -S "$ROOT_DIR" -B "$build_dir" \
    -DCMAKE_INSTALL_PREFIX="$install_prefix" \
    -DKDE="$kde_flag" \
    -DCMAKE_BUILD_TYPE="$build_type"

  cmake --build "$build_dir" --parallel "$parallel"
}

case "$mode" in
  kde)
    build_desktop "kde" "ON"
    ;;
  nonkde|nokde)
    build_desktop "nonkde" "OFF"
    ;;
  android)
    "$SCRIPT_DIR/android/build.sh"
    ;;
  all)
    build_desktop "kde" "ON"
    build_desktop "nonkde" "OFF"
    ANDROID_OPTIONAL=1 "$SCRIPT_DIR/android/build.sh"
    ;;
  *)
    die "usage: $0 {kde|nonkde|android|all}"
    ;;
esac
