#!/usr/bin/env bash
# Regenerates patches/tracy-vxworks.patch from the working tree in ./tracy.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
git -C "$ROOT/tracy" diff HEAD > "$ROOT/patches/tracy-vxworks.patch"
git -C "$ROOT/tracy" diff HEAD --stat
