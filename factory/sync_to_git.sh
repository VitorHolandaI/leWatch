#!/usr/bin/env bash
# Copia o código de factory (working) → repo git (onde os commits acontecem).
# Protege .git, CLAUDE.md, AGENTS.md e factory/ do destino (não existem na fonte).
# Honra .gitignore da fonte, então build junk (*.o, build/, etc.) não vai junto.
set -euo pipefail

SRC="/home/vitor/Arduino/factory"
DST="/home/vitor/tinker_git/leWatch/watch"

rsync -a --delete \
  --filter=':- .gitignore' \
  --exclude='.git/' \
  --exclude='.cache/' \
  --exclude='CLAUDE.md' \
  --exclude='AGENTS.md' \
  --exclude='/factory' \
  "$SRC/" "$DST/"

# Traz CLAUDE.md e AGENTS.md do repo de volta pra fonte.
cp -f "$DST/CLAUDE.md" "$DST/AGENTS.md" "$SRC/"

echo "synced $SRC -> $DST (CLAUDE.md/AGENTS.md copiados de volta p/ fonte)"
