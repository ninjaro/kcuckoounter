#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"

mode="${1:-}"

if [ -z "$mode" ]; then
  die "usage: $0 {kde|nonkde}"
fi

case "$mode" in
  kde)
    bin="$ROOT_DIR/build-kde/kcuckoounter"
    ;;
  nonkde|nokde)
    bin="$ROOT_DIR/build-nonkde/kcuckoounter"
    if [ ! -x "$bin" ] && [ -x "$ROOT_DIR/build-nokde/kcuckoounter" ]; then
      bin="$ROOT_DIR/build-nokde/kcuckoounter"
    fi
    ;;
  *)
    die "usage: $0 {kde|nonkde}"
    ;;
esac

if [ ! -x "$bin" ]; then
  die "binary $bin not found; run ./scripts/cli.sh build $mode first"
fi

time_cmd=()
if [ -x /usr/bin/time ]; then
  if /usr/bin/time -v true >/dev/null 2>&1; then
    time_cmd=(/usr/bin/time -v)
  elif /usr/bin/time -l true >/dev/null 2>&1; then
    time_cmd=(/usr/bin/time -l)
  else
    time_cmd=(/usr/bin/time)
  fi
else
  time_cmd=(time -p)
fi

log "Running $bin with memory usage statistics..."
"${time_cmd[@]}" "$bin"
