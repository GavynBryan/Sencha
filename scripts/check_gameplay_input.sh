#!/usr/bin/env bash
#
# Fitness function: gameplay reads actions, never devices.
#
# Once the input mapper exists, a gameplay system that reaches for InputFrame or
# a scancode is reintroducing the thing the mapper replaced: a control that
# cannot be rebound, cannot be shadowed by a context, and has no defined
# behaviour on a frame that runs no fixed tick. Physical input stays below the
# mapping layer, in engine/src/input, the platform layer, and the editors.
#
# Window management is not input mapping: a game may still handle raw platform
# events for things like mouse capture, which is why SDL_BUTTON_* in an
# OnPlatformEvent handler is not matched here. Reading device *state* is what
# this forbids.
#
# Usage: check_gameplay_input.sh <source-root>

set -uo pipefail

ROOT="${1:-.}"
status=0

# Gameplay-owning trees: engine feature dirs that model gameplay, the game
# template every new project is copied from, and the example hosts, which read
# actions like any other consumer even though their controls are built in code
# rather than authored.
GAMEPLAY_DIRS=(
    "$ROOT/engine/src/controller"
    "$ROOT/engine/include/controller"
    "$ROOT/example/CubeDemo"
    "$ROOT/example/SceneViewer"
    "$ROOT/engine/src/movement"
    "$ROOT/engine/include/movement"
    "$ROOT/engine/src/abilities"
    "$ROOT/engine/include/abilities"
    "$ROOT/engine/src/attributes"
    "$ROOT/engine/include/attributes"
    "$ROOT/engine/src/effects"
    "$ROOT/engine/include/effects"
    "$ROOT/engine/src/camera"
    "$ROOT/engine/include/camera"
    "$ROOT/template/src"
)

EXISTING=()
for dir in "${GAMEPLAY_DIRS[@]}"; do
    [ -d "$dir" ] && EXISTING+=("$dir")
done

if [ "${#EXISTING[@]}" -eq 0 ]; then
    echo "gameplay input discipline: no gameplay directories found"
    exit 0
fi

# SDL_GetKeyboardState is called out by name: polling the device directly
# bypasses even the raw snapshot, so nothing above the platform layer may.
#
# Raw platform events are not matched. A host still handles window concerns like
# mouse capture in OnPlatformEvent, which is below the mapping layer, not beside
# it. Reading device *state* is what this forbids.
hits="$(grep -rnE 'InputFrame|SDL_SCANCODE|SDL_GetKeyboardState|IsKeyDown|IsMouseButtonDown|\bctx\.Input\b' "${EXISTING[@]}" 2>/dev/null \
        | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)')"

if [ -n "$hits" ]; then
    echo "VIOLATION: gameplay code reading raw device input"
    echo "$hits"
    echo
    echo "Read actions instead: InputActionState::Tick() in a fixed-tick system,"
    echo "Frame() on the presentation clock. Bind the control in an"
    echo "input.profile asset rather than naming a key in code."
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "gameplay input discipline: OK"
fi
exit "$status"
