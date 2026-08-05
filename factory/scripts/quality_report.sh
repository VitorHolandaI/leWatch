#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

run_build=0
run_smoke=0
strict=0

for arg in "$@"; do
  case "$arg" in
    --build) run_build=1 ;;
    --smoke) run_smoke=1 ;;
    --strict) strict=1 ;;
    -h|--help)
      printf 'Usage: %s [--build] [--smoke] [--strict]\n' "$0"
      printf 'Compact code-quality report. Full logs are stored in /tmp.\n'
      exit 0
      ;;
    *)
      printf 'Unknown option: %s\n' "$arg" >&2
      exit 2
      ;;
  esac
done

log_dir="$(mktemp -d /tmp/factory-quality.XXXXXX)"
status=0

section() { printf '\n## %s\n' "$1"; }

check() {
  name="$1"
  shift
  log="$log_dir/${name// /_}.log"
  if "$@" >"$log" 2>&1; then
    printf '[ok]   %s\n' "$name"
  else
    code=$?
    status=1
    printf '[fail] %s (exit %s, log: %s)\n' "$name" "$code" "$log"
    grep -E '(^|[^[:alpha:]])(error|warning|style|performance|portability|complex|too long|failed)([^[:alpha:]]|$)' -i "$log" | head -n 8 || true
  fi
}

section "Resumo"
printf 'repo: %s\n' "$ROOT"
printf 'logs: %s\n' "$log_dir"

section "Inventario"
if command -v rg >/dev/null 2>&1; then
  files="$(rg --files -g '*.cpp' -g '*.h' -g '*.ino' -g '!src/*.c' -g '!audio/**' | sort)"
else
  files="$(find . -name '*.cpp' -o -name '*.h' -o -name '*.ino' | sed 's#^./##' | sort)"
fi

printf 'arquivos C/C++: %s\n' "$(printf '%s\n' "$files" | sed '/^$/d' | wc -l | tr -d ' ')"
printf 'linhas C/C++:   %s\n' "$(printf '%s\n' "$files" | xargs -r wc -l | tail -n 1 | awk '{print $1}')"
printf 'TODO/FIXME:     %s\n' "$(grep -RInE 'TODO|FIXME|HACK|XXX' -- *.cpp *.h *.ino 2>/dev/null | wc -l | tr -d ' ')"

section "Maiores arquivos"
printf '%s\n' "$files" | xargs -r wc -l | awk '$2 != "total"' | sort -nr | head -n 8 | awk '{printf "%6s  %s\n", $1, $2}'

section "Checks"
if [ "$run_build" -eq 1 ]; then
  check "quality build" make -C sim quality-build
else
  printf '[skip] quality build (use --build)\n'
fi

if command -v cppcheck >/dev/null 2>&1; then
  check "cppcheck" make -C sim cppcheck
else
  printf '[skip] cppcheck not installed\n'
fi

if command -v uv >/dev/null 2>&1; then
  if [ "$strict" -eq 1 ]; then
    check "lizard strict" make -C sim lizard-strict
  else
    check "lizard" make -C sim lizard
  fi
else
  printf '[skip] uv/lizard not available\n'
fi

if [ "$run_smoke" -eq 1 ]; then
  check "sim smoke" make -C sim smoke
else
  printf '[skip] sim smoke (use --smoke)\n'
fi

section "Resultado"
if [ "$status" -eq 0 ]; then
  printf 'qualidade: ok\n'
else
  printf 'qualidade: revisar falhas acima\n'
fi

exit "$status"
