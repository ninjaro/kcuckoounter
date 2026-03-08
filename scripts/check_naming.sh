#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"

mode="${1:-changed}"
case "$mode" in
  changed|all) ;;
  *)
    die "usage: $0 {changed|all}"
    ;;
esac

declare -a keywords=(
  alignas alignof and and_eq asm auto bitand bitor bool break case catch
  char char8_t char16_t char32_t class compl concept const consteval constexpr
  constinit const_cast continue co_await co_return co_yield decltype default
  delete do double dynamic_cast else enum explicit export extern false float
  for friend goto if inline int long mutable namespace new noexcept not not_eq
  nullptr operator or or_eq private protected public register reinterpret_cast
  requires return short signed sizeof static static_assert static_cast struct
  switch template this thread_local throw true try typedef typeid typename union
  unsigned using virtual void volatile wchar_t while xor xor_eq
)

keyword_regex="^($(IFS='|'; echo "${keywords[*]}"))$"
allowlist_file="$SCRIPT_DIR/naming_allowlist.txt"

declare -A allowlist=()
if [[ -f "$allowlist_file" ]]; then
  while IFS= read -r line; do
    line="${line%%#*}"
    line="$(xargs <<<"$line" || true)"
    [[ -z "$line" ]] && continue
    allowlist["$line"]=1
  done < "$allowlist_file"
fi

collect_input() {
  if [[ "$mode" == "all" ]]; then
    rg --no-heading --line-number '.*' "$ROOT_DIR/src" "$ROOT_DIR/include" "$ROOT_DIR/tests" \
      | sed -E "s#^$ROOT_DIR/##"
    return
  fi

  git -C "$ROOT_DIR" diff --unified=0 -- src include tests \
    | awk '
      /^\+\+\+ b\// { file = substr($0, 7); next }
      /^\+[^+]/ && file != "" { print file ":" substr($0, 2) }
    '
}

declare -A seen=()
declare -a violations=()

while IFS= read -r row; do
  [[ -z "$row" ]] && continue
  file="${row%%:*}"
  text="${row#*:}"

  while IFS= read -r ident; do
    [[ -z "$ident" ]] && continue
    if [[ "$ident" =~ $keyword_regex ]]; then
      continue
    fi
    if [[ -n "${allowlist[$ident]:-}" ]]; then
      continue
    fi

    words=1
    if [[ "$ident" == *_* ]]; then
      underscore_count="${ident//[^_]}"
      words=$(( ${#underscore_count} + 1 ))
    fi

    limit=5
    if [[ "$file" == tests/* ]]; then
      limit=8
    fi
    if (( words <= limit )); then
      continue
    fi

    key="$file|$ident|$words"
    if [[ -n "${seen[$key]:-}" ]]; then
      continue
    fi
    seen[$key]=1
    violations+=("$file: identifier '$ident' has $words words (limit: $limit)")
  done < <(grep -Eo '\b[a-z][a-z0-9_]*\b' <<<"$text" || true)
done < <(collect_input)

if [[ "${#violations[@]}" -eq 0 ]]; then
  log "Naming check passed for mode '$mode'."
  exit 0
fi

log "Naming check found ${#violations[@]} violations (mode: $mode):"
for line in "${violations[@]}"; do
  log "  - $line"
done
exit 1
