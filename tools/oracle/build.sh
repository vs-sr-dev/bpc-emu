#!/bin/sh
# builds tools/oracle/oracle.exe from wskit's runtime (WSKIT: where wskit is, ../wskit by
# default) with MSYS2's ucrt64 g++
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
WSKIT=${WSKIT:-$HERE/../../../wskit}
RT=$WSKIT/runtime
export PATH=/c/msys64/ucrt64/bin:$PATH
OUT=$HERE/../../build/oracle
mkdir -p "$OUT"
g++ -std=c++17 -O2 -w -I"$RT" -o "$OUT/oracle.exe" "$HERE/oracle.cpp" \
  "$RT/ws_core.cpp" "$RT/ws_alu.cpp" "$RT/ws_video.cpp" "$RT/ws_sound.cpp" "$RT/ws_serial.cpp" \
  "$RT/none.cpp" -static-libgcc -static-libstdc++ -lws2_32
echo "$OUT/oracle.exe"
