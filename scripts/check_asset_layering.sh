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

# The front door is generic over kind: it holds a registry of records, and every
# operation is addressed by AssetType with an AssetLease crossing the boundary.
# A loader, a cache, or a typed handle in its header would be a kind it names,
# and the switch-per-driver this replaced would grow back one include at a time.
FRONT_DOOR="$ROOT/engine/include/assets/runtime/AssetSystem.h"
named="$(grep -nE '#include[[:space:]]*[\"<]([^\">]*/)?([A-Za-z]*Loader|[A-Za-z]*Cache|[A-Za-z]*Handle)\.h' \
              "$FRONT_DOOR" 2>/dev/null)"
if [ -n "$named" ]; then
    echo "VIOLATION: the asset front door names an asset kind (keep it generic over AssetType)"
    echo "$named"
    echo
    status=1
fi

# Asset fields are read and written through the schema's AssetType and arity,
# never by naming the kinds a document might hold. A concrete handle header here
# means one kind got special treatment, which is the shape both of these were
# rewritten to remove.
FIELD_IO="$ROOT/engine/src/world/serialization/SceneAssetFieldIo.cpp
$ROOT/editor/kyusu/src/document/AssetFieldIo.cpp"
while read -r unit; do
    [ -f "$unit" ] || continue
    named="$(grep -nE '#include[[:space:]]*[\"<]([^\">]*/)?([A-Za-z]*Handle|[A-Za-z]*Cache)\.h' \
                  "$unit" 2>/dev/null)"
    if [ -n "$named" ]; then
        echo "VIOLATION: ${unit#"$ROOT"/} names a concrete asset kind (go through AssetType and arity)"
        echo "$named"
        echo
        status=1
    fi
done <<< "$FIELD_IO"

if [ "$status" -eq 0 ]; then
    echo "asset layering directions: OK"
fi
exit "$status"
