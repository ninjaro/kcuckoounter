#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"

mode="${1:-}"

if [ -z "$mode" ]; then
  die "usage: $0 {kde|nonkde|android-emulator|android-device}"
fi

case "$mode" in
  kde)
    bin="$ROOT_DIR/build-kde/kcuckoounter"
    if [ ! -x "$bin" ]; then
      die "binary $bin not found; run ./scripts/cli.sh build kde first"
    fi
    exec "$bin"
    ;;
  nonkde|nokde)
    bin="$ROOT_DIR/build-nonkde/kcuckoounter"
    if [ ! -x "$bin" ] && [ -x "$ROOT_DIR/build-nokde/kcuckoounter" ]; then
      bin="$ROOT_DIR/build-nokde/kcuckoounter"
    fi
    if [ ! -x "$bin" ]; then
      die "binary $bin not found; run ./scripts/cli.sh build nonkde first"
    fi
    exec "$bin"
    ;;
  android-emulator)
    exec "$SCRIPT_DIR/android/run_emulator.sh"
    ;;
  android-device)
    exec "$SCRIPT_DIR/android/run_device.sh"
    ;;
  *)
    die "usage: $0 {kde|nonkde|android-emulator|android-device}"
    ;;
esac
