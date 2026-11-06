#!/bin/bash
# Windows release package: dist/bpc-emu-<version>-win64.zip with bpc-emu.exe, bpc-cli.exe, SDL2.dll, the
# README and the licences (after Flick's, same author). A clean Release build in build-release/
# with the version suffix removed. No cartridge or save goes in: bring your own.
# Needs MSYS2 mingw64 (gcc, cmake, ninja, SDL2) and a Python (only its zipfile module).
# Refuses to package uncommitted changes unless ALLOW_DIRTY=1.
set -e
PY=${PY:-$(command -v python)}      # resolve before mingw64 joins PATH
cd "$(dirname "$0")/.."
export PATH=/c/msys64/mingw64/bin:$PATH
MINGW=${MINGW:-/c/msys64/mingw64}

if [ -n "$(git status --porcelain)" ] && [ "${ALLOW_DIRTY:-0}" != 1 ]; then
  echo "uncommitted changes: commit them first (or ALLOW_DIRTY=1)" >&2
  exit 1
fi

rm -rf build-release
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBPC_VERSION_SUFFIX= >/dev/null
cmake --build build-release --target bpc-emu bpc-cli
VER=$(build-release/bpc-emu.exe --version | sed 's/^bpc-emu //' | tr -d '\r')
NAME=bpc-emu-$VER-win64
OUT=dist/$NAME

rm -rf "$OUT" "dist/$NAME.zip"
mkdir -p "$OUT"
cp build-release/bpc-emu.exe build-release/bpc-cli.exe "$OUT/"
strip "$OUT/bpc-emu.exe" "$OUT/bpc-cli.exe"
cp "$MINGW/bin/SDL2.dll" "$OUT/"
cp README.md LICENSE THIRD_PARTY_NOTICES.md "$OUT/"
cp "$MINGW/share/licenses/SDL2/LICENSE.txt" "$OUT/LICENSE-SDL2.txt"

# nothing copyrighted may slip in
if ls "$OUT" | grep -qiE '\.(pc2|ws|wsc|sav|bin|zip|rom)$'; then
  echo "a dump ended up in $OUT: refusing to package" >&2
  exit 1
fi

(cd dist && "$PY" -m zipfile -c "$NAME.zip" "$NAME")
echo "dist/$NAME.zip ($(git rev-parse --short HEAD))"
ls -l "$OUT"
