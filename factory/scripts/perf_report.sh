#!/usr/bin/env bash
# Performance / complexity analysis for the firmware.
#   - cyclomatic-complexity & length hotspots (lizard)
#   - performance-category static checks (cppcheck)
#   - code + binary size
#
# Usage: scripts/perf_report.sh [--top N]   (default N=12)
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

top=12
if [ "${1:-}" = "--top" ] && [ -n "${2:-}" ]; then top="$2"; fi

# Quoted globs in an array so the shell does not expand -x patterns.
LIZARD_ARGS=(-l cpp -C 15 -L 80 -x './src/*' -x './audio/*')

printf '## Hotspots (worst %s by cyclomatic complexity)\n' "$top"
if command -v uv >/dev/null 2>&1; then
  # Table columns: NLOC CCN token PARAM length location -> sort desc by CCN.
  uv run lizard "${LIZARD_ARGS[@]}" . 2>/dev/null \
    | awk 'NF==6 && $1 ~ /^[0-9]+$/ && $6 ~ /@/ {print}' \
    | sort -k2 -rn \
    | head -n "$top" \
    | awk '{printf "  CCN %-4s NLOC %-4s len %-4s %s\n", $2, $1, $5, $6}'
  printf '\n## Threshold violations (CCN>15 or len>80)\n'
  v=$(uv run lizard "${LIZARD_ARGS[@]}" -w . 2>/dev/null)
  [ -n "$v" ] && printf '%s\n' "$v" | head -n 20 || printf '  none\n'
else
  printf '[skip] uv/lizard not available\n'
fi

printf '\n## cppcheck performance category\n'
if command -v cppcheck >/dev/null 2>&1; then
  cppcheck --enable=performance --std=c++17 --inline-suppr \
    --suppress=missingIncludeSystem \
    -DARDUINO_T_WATCH_S3 -DUSING_IR_REMOTE -DESP_IDF_VERSION=0 \
    '-DESP_IDF_VERSION_VAL(a,b,c)=0' \
    *.cpp sim/stubs.cpp 2>&1 | grep -E 'performance:' | head -n 20 || printf '  none\n'
else
  printf '[skip] cppcheck not installed\n'
fi

printf '\n## Size\n'
loc=$(find . -maxdepth 1 -name '*.cpp' -o -maxdepth 1 -name '*.h' -o -maxdepth 1 -name '*.ino' 2>/dev/null | xargs -r wc -l | tail -n1 | awk '{print $1}')
printf '  firmware source LOC: %s\n' "$loc"
elf=$(find build -name '*.elf' 2>/dev/null | head -1)
if [ -n "$elf" ] && command -v size >/dev/null 2>&1; then
  printf '  firmware .elf:\n'
  size "$elf" | sed 's/^/    /'
else
  printf '  firmware .elf: (none built — compile in Arduino IDE to populate build/)\n'
fi
[ -f sim/sim ] && printf '  sim binary: %s\n' "$(du -h sim/sim | awk '{print $1}')"

printf '\nNote: true runtime/power profiling needs hardware (Joulescope/INA226).\n'
