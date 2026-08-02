#!/usr/bin/env bash
#
# Asset layering guard (docs/assets/architecture.md), enforced as a source scan.
# Same form as check_editor_layering.sh: a ctest that fails on a violation.
#
# core/assets holds the dependency-neutral half of the asset system -- identity
# (AssetId, AssetPath, AssetRef), the registry and manifest, the byte source,
# the staging contract, and the handle-storage base. Those types are reusable by
# anything, including the cook, and must not reach into the layers that consume
# them.
#
# The integration half lives in assets/runtime (AssetSystem, AssetPreloader,
# RuntimeAssets). It legitimately depends on caches, render and GPU resources,
# audio, animation, and the job lane, which is exactly why it does not belong
# under core/.
#
# Usage: check_asset_layering.sh <source-root>

set -uo pipefail

ROOT="${1:-.}"
CORE_INC="$ROOT/engine/include/core/assets"
CORE_SRC="$ROOT/engine/src/core/assets"
status=0

# Layers core/assets must not import. Consumers of asset identity, not
# dependencies of it.
FORBIDDEN='app|jobs|audio|anim|render|graphics|world|zone|physics|movement|abilities|attributes|camera'

# Drops comment-only lines so prose naming a forbidden layer is not flagged.
hits="$(grep -rnE "#include[[:space:]]*[\"<]([^\">]*/)?($FORBIDDEN)/" "$CORE_INC" "$CORE_SRC" 2>/dev/null \
        | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)')"

if [ -n "$hits" ]; then
    echo "VIOLATION: core/assets imports a consumer layer (move the type to assets/runtime instead)"
    echo "$hits"
    echo
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "asset layering directions: OK"
fi
exit "$status"
