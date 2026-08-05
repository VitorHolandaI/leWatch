#!/usr/bin/env bash
# Generate project documentation:
#   - Graphviz power-up / power-state diagram (docs/power_up.png + .svg)
#   - Doxygen HTML with UML class diagrams, call graphs and include graphs
#
# Usage: scripts/gen_docs.sh [--open]
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

open_after=0
for arg in "$@"; do
  case "$arg" in
    --open) open_after=1 ;;
    -h|--help) printf 'Usage: %s [--open]\n' "$0"; exit 0 ;;
    *) printf 'Unknown option: %s\n' "$arg" >&2; exit 2 ;;
  esac
done

status=0

# ---- 1. diagrams ----
printf '## Diagrams\n'
if command -v dot >/dev/null 2>&1; then
  for d in docs/*.dot; do
    [ -e "$d" ] || continue
    base="${d%.dot}"
    if dot -Tpng "$d" -o "$base.png" && dot -Tsvg "$d" -o "$base.svg"; then
      printf '[ok]   %s -> %s.{png,svg}\n' "$d" "$base"
    else
      printf '[fail] %s\n' "$d"; status=1
    fi
  done
else
  printf '[skip] graphviz "dot" not installed (pacman -S graphviz)\n'
fi

# ---- 2. doxygen ----
printf '\n## Doxygen\n'
if command -v doxygen >/dev/null 2>&1; then
  if doxygen Doxyfile >/tmp/doxygen.log 2>&1; then
    html="$ROOT/docs/doxygen/html/index.html"
    printf '[ok]   %s\n' "$html"
    printf '       class tree: hierarchy.html | includes: files.html | call graphs: per-function pages\n'
    [ "$open_after" -eq 1 ] && command -v xdg-open >/dev/null 2>&1 && xdg-open "$html" >/dev/null 2>&1 &
  else
    printf '[fail] doxygen (see /tmp/doxygen.log)\n'; status=1
  fi
else
  printf '[skip] doxygen not installed (pacman -S doxygen)\n'
fi

printf '\n## Resultado\n'
[ "$status" -eq 0 ] && printf 'docs: ok\n' || printf 'docs: revisar falhas acima\n'
exit "$status"
