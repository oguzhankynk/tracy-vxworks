#!/usr/bin/env bash
# Reproduces the whole prototype:
#   1. fetch Tracy (tested tag) and apply patches/tracy-vxworks.patch
#   2. build the Tracy client + demo RTP for VxWorks (SDK toolchain)
#   3. build Tracy's capture and csvexport tools for the host
#   4. boot the SDK kernel in QEMU, run the demo, capture a trace on the host
#   5. check the trace with csvexport
#
# Environment:
#   WIND_HOME   VxWorks SDK directory (required)
#   HOST_PORT   host TCP port forwarded to the guest's Tracy port (default 18086)
#   FRAMES      frames the demo runs (default 200)
#   KVM=1       boot with KVM and -cpu host,+invtsc instead of TCG
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${WIND_HOME:?set WIND_HOME to the VxWorks SDK directory}"
export WIND_HOME
HOST_PORT="${HOST_PORT:-18086}"
FRAMES="${FRAMES:-200}"
VX_BUILD="$ROOT/build/vx"
OUT="$ROOT/build/trace"
export CPM_SOURCE_CACHE="${CPM_SOURCE_CACHE:-$ROOT/build/cpm-cache}"

"$ROOT/scripts/fetch_tracy.sh"

echo "== VxWorks RTP build"
cmake -S "$ROOT" -B "$VX_BUILD" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/vxworks.cmake" >/dev/null
cmake --build "$VX_BUILD"

echo "== host tools (capture, csvexport, trace-info)"
if [[ ! -x "$ROOT/build/host-capture/tracy-capture" ]]; then
  cmake -S "$ROOT/tracy/capture" -B "$ROOT/build/host-capture" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$ROOT/build/host-capture" --target tracy-capture
fi
if [[ ! -x "$ROOT/build/host-csvexport/tracy-csvexport" ]]; then
  cmake -S "$ROOT/tracy/csvexport" -B "$ROOT/build/host-csvexport" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$ROOT/build/host-csvexport"
fi

if [[ ! -x "$ROOT/build/host-trace-info/tracy-trace-info" ]]; then
  cmake -S "$ROOT/tools/trace_info" -B "$ROOT/build/host-trace-info" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$ROOT/build/host-trace-info"
fi

echo "== QEMU run + capture"
mkdir -p "$OUT"
kvm_opt=()
[[ "${KVM:-0}" == 1 ]] && kvm_opt=(--kvm)
python3 "$ROOT/tools/run_tracy_demo.py" \
  --build-dir "$VX_BUILD" \
  --capture "$ROOT/build/host-capture/tracy-capture" \
  --output "$OUT/tracy_demo.tracy" \
  --host-port "$HOST_PORT" --frames "$FRAMES" \
  --pid-file "$OUT/qemu.pid" "${kvm_opt[@]}"

echo "== verify"
echo "-- csvexport (zone statistics; columns: name, src_line, counts)"
"$ROOT/build/host-csvexport/tracy-csvexport" "$OUT/tracy_demo.tracy" | cut -d, -f1,3,6
echo "-- trace-info"
"$ROOT/build/host-trace-info/tracy-trace-info" --expect-frames "$FRAMES" "$OUT/tracy_demo.tracy"
