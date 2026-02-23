#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"

mode="${1:-nonkde}"
run_tests=false

if [[ "${2:-}" == "--tests" ]]; then
  run_tests=true
fi

case "$mode" in
  kde)
    build_dir="$ROOT_DIR/build-leakcheck-kde"
    kde_flag="ON"
    bin="$build_dir/kcuckoounter"
    ;;
  nonkde|nokde)
    build_dir="$ROOT_DIR/build-leakcheck-nokde"
    kde_flag="OFF"
    bin="$build_dir/kcuckoounter"
    ;;
  *)
    die "usage: $0 {kde|nonkde} [--tests]"
    ;;
esac

sanitizer_flags="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"

cmake -S "$ROOT_DIR" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DKDE="$kde_flag" \
  -DBUILD_UNIT_TESTS="$([[ "$run_tests" == true ]] && echo ON || echo OFF)" \
  -DCMAKE_C_FLAGS="$sanitizer_flags" \
  -DCMAKE_CXX_FLAGS="$sanitizer_flags" \
  -DCMAKE_EXE_LINKER_FLAGS="$sanitizer_flags" \
  -DCMAKE_SHARED_LINKER_FLAGS="$sanitizer_flags"

cmake --build "$build_dir" --parallel "$(nproc_safe)"

export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"
export LSAN_OPTIONS="exitcode=1"

if [[ "$run_tests" == true ]]; then
  log "Running leak checks via unit tests..."
  ctest --test-dir "$build_dir" --output-on-failure
else
  if [ ! -x "$bin" ]; then
    die "binary $bin not found; build failed"
  fi
  log "Running leak checks via application (close the app to finish)..."
  "$bin"
fi
