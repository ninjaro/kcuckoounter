#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"

mode="${1:-all}"
build_type="${BUILD_TYPE:-Debug}"
install_prefix="${INSTALL_PREFIX:-$HOME/kde/usr}"
parallel="${PARALLEL:-$(nproc_safe)}"

run_tests() {
  local name="$1"
  local kde_flag="$2"
  local junit_output_override="${3:-}"
  local build_dir="$ROOT_DIR/build-$name"
  local junit_output="${junit_output_override:-${JUNIT_OUTPUT:-}}"

  cmake -S "$ROOT_DIR" -B "$build_dir" \
    -DCMAKE_INSTALL_PREFIX="$install_prefix" \
    -DKDE="$kde_flag" \
    -DBUILD_UNIT_TESTS=ON \
    -DCMAKE_BUILD_TYPE="$build_type"

  cmake --build "$build_dir" --parallel "$parallel"
  if [[ -n "$junit_output" ]]; then
    ctest --test-dir "$build_dir" --output-on-failure --parallel "$parallel" \
      --output-junit "$junit_output"
  else
    ctest --test-dir "$build_dir" --output-on-failure --parallel "$parallel"
  fi
}

junit_output_for_mode() {
  local base_path="$1"
  local mode_name="$2"
  if [[ -z "$base_path" ]]; then
    echo ""
    return 0
  fi

  local extension="${base_path##*.}"
  if [[ "$extension" == "$base_path" ]]; then
    echo "${base_path}-${mode_name}.xml"
    return 0
  fi

  local stem="${base_path%.*}"
  echo "${stem}-${mode_name}.${extension}"
}

case "$mode" in
  kde)
    run_tests "kde" "ON"
    ;;
  nonkde|nokde)
    run_tests "nonkde" "OFF"
    ;;
  all)
    junit_base="${JUNIT_OUTPUT:-}"
    run_tests "kde" "ON" "$(junit_output_for_mode "$junit_base" "kde")"
    run_tests "nonkde" "OFF" "$(junit_output_for_mode "$junit_base" "nonkde")"
    ;;
  *)
    die "usage: $0 {kde|nonkde|all}"
    ;;
esac
