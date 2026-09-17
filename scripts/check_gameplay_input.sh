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
# Window management is not input mapping: a host may still handle raw platform
# events, which is why SDL_BUTTON_* in an OnPlatformEvent handler is not matched
# here. Reading device *state* is what this forbids.
#
# The exemption used to carry the templates' hold-right-button-to-look
# convention. It no longer does: gameplay states a standing capture request once
# and the application shell releases and restores it, so no template handles a
# platform event at all. What is left under the exemption is example/CubeDemo's
# free camera, which is a diagnostic tool with an editor's gesture rather than a
# game.
#
# Usage: check_gameplay_input.sh <source-root>

set -uo pipefail

ROOT="${1:-.}"
status=0

# Gameplay-owning trees: engine feature dirs that model gameplay, every starter
# template a new project is copied from, and the example hosts, which read
# actions like any other consumer even though their controls are built in code
# rather than authored.
GAMEPLAY_DIRS=(
    "$ROOT/engine/src/controller"
    "$ROOT/engine/include/controller"
    "$ROOT/example/CubeDemo"
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
)
for template_src in "$ROOT"/templates/*/src; do
    [ -d "$template_src" ] && GAMEPLAY_DIRS+=("$template_src")
done

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
# SDL_SetWindowRelativeMouseMode is called out for the same reason from the
# other end: pointer capture is arbitrated against window focus, the debug
# overlay and the application shell, and a caller that sets the mode itself
# silently opts out of all three. Ask Engine::SetPointerCaptured instead.
hits="$(grep -rnE 'InputFrame|SDL_SCANCODE|SDL_GetKeyboardState|SDL_SetWindowRelativeMouseMode|SDL_SetRelativeMouseMode|IsKeyDown|IsMouseButtonDown|\bctx\.Input\b' "${EXISTING[@]}" 2>/dev/null \
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
