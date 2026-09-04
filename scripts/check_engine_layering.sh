#!/usr/bin/env bash
#
# Engine layering guards, enforced as a source scan so ownership cannot silently
# regress. Same form as check_editor_layering.sh: a ctest that fails on a
# violation and names the file.
#
# The rules here are about which layer owns a decision, not about which names are
# allowed. Each one records a boundary that took work to establish:
#
#   A. The engine knows no game. Nothing under engine/, example/, or editor/ may
#      include a game module's sources. A template is the engine's consumer, and
#      the engine never observes one.
#
#   B. Content mounting is an asset-layer concern. ContentMount fills an asset
#      registry and nothing else, so it must not reach into the application,
#      world, or configuration layers -- the editor calls it without any of them.
#
# Usage: check_engine_layering.sh <source-root>

set -uo pipefail

ROOT="${1:-.}"
ENGINE="$ROOT/engine"
status=0

# Greps for a pattern but drops comment-only lines, so prose mentioning a
# forbidden name is not flagged.
check() {
    local desc="$1" pattern="$2"
    shift 2
    local hits
    hits="$(grep -rnE "$pattern" "$@" 2>/dev/null \
            | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)')"
    if [ -n "$hits" ]; then
        echo "VIOLATION: $desc"
        echo "$hits"
        echo
        status=1
    fi
}

# A. No engine, example, or editor source includes a game module's sources. The
# game directories are named explicitly rather than by wildcard so that adding a
# game is a deliberate edit here.
for tree in "$ENGINE" "$ROOT/example" "$ROOT/editor"; do
    [ -d "$tree" ] || continue
    check "engine-side code includes a game module's sources ($tree)" \
          '#include[[:space:]]*["<]([^">]*/)?(template|templates)/' \
          "$tree"
done

# B. A loaded level publishes content and stops there. Including camera,
# participant, or input headers would mean it had started deciding what a game
# does with what it loaded, which is the boundary this whole split is about.
# Hot reload is content's, not a level's, so those headers stay out too.
check "LoadedLevel decides what a game does with a level" \
      '#include[[:space:]]*["<](camera|participant|input|controller|assets/hotreload)/' \
      "$ENGINE/include/app/LoadedLevel.h" \
      "$ENGINE/src/app/LoadedLevel.cpp"

# C. The content mount is an asset-layer primitive. It takes a RuntimeAssets&
# and an already-resolved root; anything it needed from app/, world/, or
# core/config/ would be a decision it is not entitled to make.
check "ContentMount reaches outside the asset layer" \
      '#include[[:space:]]*["<](app|world|core/config)/' \
      "$ENGINE/include/assets/runtime/ContentMount.h" \
      "$ENGINE/src/assets/runtime/ContentMount.cpp"

exit $status
