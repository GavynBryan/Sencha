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
#   The rest are per-layer include-direction rules, each explained where it
#   is checked.
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

# B. The participant layer publishes identity: who is here, and which entity
# each of them drives. What a game switches on because of that -- whose look
# input a body takes, which camera follows -- is the game's composition rule,
# so nothing here may reach into controller/, camera/, net/, or zone/.
check "participant/ reaches into a layer that consumes it" \
      '#include[[:space:]]*["<](controller|camera|net|zone)/' \
      "$ENGINE/include/participant" "$ENGINE/src/participant"

# C. The camera layer is a component and a query. How a camera is placed, what
# it follows, whose input turns it -- all of that is a game's, so camera/ may
# not read controller, input, movement, or participant state.
check "camera/ encodes a camera policy" \
      '#include[[:space:]]*["<](controller|input|movement|participant)/' \
      "$ENGINE/include/camera" "$ENGINE/src/camera"

# D. A loaded level publishes content and stops there. Including camera,
# participant, or input headers would mean it had started deciding what a game
# does with what it loaded, which is the boundary this whole split is about.
# Hot reload is content's, not a level's, so those headers stay out too.
check "LoadedLevel decides what a game does with a level" \
      '#include[[:space:]]*["<](camera|participant|input|controller|assets/hotreload)/' \
      "$ENGINE/include/app/LoadedLevel.h" \
      "$ENGINE/src/app/LoadedLevel.cpp"

# E. The content mount is an asset-layer primitive. It takes a RuntimeAssets&
# and an already-resolved root; anything it needed from app/, world/, or
# core/config/ would be a decision it is not entitled to make.
check "ContentMount reaches outside the asset layer" \
      '#include[[:space:]]*["<](app|world|core/config)/' \
      "$ENGINE/include/assets/runtime/ContentMount.h" \
      "$ENGINE/src/assets/runtime/ContentMount.cpp"

# F. Where a body comes from is nobody's business below app/. The participant
# layer asks a policy for a body and never learns that it was a scene spawn;
# the spawn service publishes groups and never learns that one was a body.
# BodySpawns is the one place both are known, and it knows
# nothing else: no session, no camera, no input, no zone policy.
check "participant/ learns how a body is produced" \
      '#include[[:space:]]*["<](runtime/spawn|world/scene|app)/' \
      "$ENGINE/include/participant" "$ENGINE/src/participant"
check "runtime/spawn/ learns what a participant is" \
      '#include[[:space:]]*["<]participant/' \
      "$ENGINE/include/runtime/spawn" "$ENGINE/src/runtime/spawn"
check "BodySpawns decides a game policy" \
      '#include[[:space:]]*["<](net|camera|controller|input|zone|core/config)/' \
      "$ENGINE/include/app/BodySpawns.h" \
      "$ENGINE/src/app/BodySpawns.cpp"

exit $status
