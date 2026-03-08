#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"

mode="${1:-apply}"

if ! command_exists rg; then
  die "rg is required for formatting; install ripgrep."
fi

if ! command_exists clang-format; then
  die "clang-format is required for formatting."
fi

case "$mode" in
  apply)
    rg --files -g '*.cpp' -g '*.hpp' -g '*.cc' -g '*.cxx' "$ROOT_DIR" \
      | xargs -r clang-format -style=file -i
    ;;
  check)
    rg --files -g '*.cpp' -g '*.hpp' -g '*.cc' -g '*.cxx' "$ROOT_DIR" \
      | xargs -r clang-format -style=file --dry-run --Werror
    ;;
  *)
    die "usage: $0 {apply|check}"
    ;;
esac
