#!/usr/bin/env bash
# Clones Tracy at the tested release tag into ./tracy and applies the
# VxWorks patch. Does nothing if ./tracy already exists.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TRACY_TAG="${TRACY_TAG:-v0.15.0}"
PATCH="$ROOT/patches/tracy-vxworks.patch"

if [[ -d "$ROOT/tracy/.git" ]]; then
  echo "fetch_tracy: $ROOT/tracy exists, leaving it alone"
  exit 0
fi
git clone --quiet --depth 1 --branch "$TRACY_TAG" https://github.com/wolfpld/tracy "$ROOT/tracy"
git -C "$ROOT/tracy" apply --whitespace=nowarn "$PATCH"
echo "fetch_tracy: Tracy $TRACY_TAG in $ROOT/tracy, patched with $(basename "$PATCH")"
