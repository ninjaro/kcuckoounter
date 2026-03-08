#!/usr/bin/env bash

COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$COMMON_DIR/../.." && pwd)"

log() {
  printf '%s\n' "$*"
}

die() {
  log "error: $*"
  exit 1
}

nproc_safe() {
  if command -v nproc >/dev/null 2>&1; then
    nproc
  elif command -v sysctl >/dev/null 2>&1; then
    sysctl -n hw.ncpu
  else
    echo 4
  fi
}

command_exists() {
  command -v "$1" >/dev/null 2>&1
}
