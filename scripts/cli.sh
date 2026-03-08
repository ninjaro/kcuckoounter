#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"

usage() {
  cat <<'EOF'
Usage: ./scripts/cli.sh <command> [args]

Commands:
  build {kde|nonkde|android|all}    Build the app
  test {kde|nonkde|all}             Build and run unit tests
  run {kde|nonkde|android-emulator|android-device}
                                   Run the app
  run-mem {kde|nonkde}              Run the app with memory usage statistics
  leaks {kde|nonkde} [--tests]      Run ASan leak checks (optionally via tests)
  deps {desktop|android|all}        Check required local CLI dependencies
  format                            Run clang-format over sources
  format-check                      Verify formatting without modifying files
  naming-check {changed|all}        Validate naming-policy word-count limits
  android env                       Print Android env export guidance
  android deps {emulator|device|build}
                                   Check Android SDK dependencies
  android build                     Build the Android APK
  android run-emulator              Install/run on emulator (start if needed)
  android run-device                Install/run on a connected device
  check                             deps+format-check+test and optional Android build
  help                              Show this help
EOF
}

cmd="${1:-help}"
shift || true

case "$cmd" in
  build)
    "$SCRIPT_DIR/build.sh" "${1:-all}"
    ;;
  test)
    "$SCRIPT_DIR/test.sh" "${1:-all}"
    ;;
  run)
    "$SCRIPT_DIR/run.sh" "${1:-}"
    ;;
  run-mem)
    "$SCRIPT_DIR/run_mem_stats.sh" "${1:-}"
    ;;
  leaks)
    "$SCRIPT_DIR/check_leaks.sh" "$@"
    ;;
  deps)
    "$SCRIPT_DIR/deps.sh" "${1:-desktop}"
    ;;
  format)
    "$SCRIPT_DIR/format.sh" apply
    ;;
  format-check)
    "$SCRIPT_DIR/format.sh" check
    ;;
  naming-check)
    "$SCRIPT_DIR/check_naming.sh" "${1:-changed}"
    ;;
  android)
    sub="${1:-}"
    shift || true
    case "$sub" in
      env)
        log "Run: source ./scripts/android/env.sh"
        ;;
      deps)
        "$SCRIPT_DIR/android/deps.sh" "${1:-emulator}"
        ;;
      build)
        "$SCRIPT_DIR/android/build.sh"
        ;;
      run-emulator)
        "$SCRIPT_DIR/android/run_emulator.sh"
        ;;
      run-device)
        "$SCRIPT_DIR/android/run_device.sh"
        ;;
      *)
        die "usage: ./scripts/cli.sh android {env|deps|build|run-emulator|run-device}"
        ;;
    esac
    ;;
  check)
    "$SCRIPT_DIR/deps.sh" desktop
    "$SCRIPT_DIR/format.sh" check
    "$SCRIPT_DIR/test.sh" all
    ANDROID_OPTIONAL=1 "$SCRIPT_DIR/android/build.sh"
    ;;
  help|-h|--help)
    usage
    ;;
  *)
    usage
    exit 1
    ;;
esac
