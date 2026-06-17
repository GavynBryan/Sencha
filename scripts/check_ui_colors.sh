#!/usr/bin/env bash
#
# Fitness function: editor UI colors come from the palette (EditorUiStyle), not
# scattered literals. Keeps the theme a single source of truth so the whole
# editor can be retuned in one place. Forbids, in editor/ui (outside
# EditorUiStyle): raw IM_COL32(...) draw-list colors, and PushStyleColor calls
# with an inline ImVec4(...) literal. (10-editor-ui-look-and-feel.md.)
#
# Usage: check_ui_colors.sh <source-root>

set -uo pipefail

ROOT="${1:-.}"
UI="$ROOT/editor/ui"
status=0

# Greps UI sources for a pattern, excluding EditorUiStyle.* and comment-only lines.
scan() {
    grep -rnE "$1" "$UI" 2>/dev/null \
        | grep -vE '/EditorUiStyle\.(h|cpp):' \
        | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)'
}

raw_col32="$(scan 'IM_COL32[[:space:]]*\(')"
if [ -n "$raw_col32" ]; then
    echo "VIOLATION: raw IM_COL32 color literal in editor/ui (use ImGui::GetColorU32(EditorUi::...))"
    echo "$raw_col32"
    echo
    status=1
fi

inline_style="$(scan 'PushStyleColor[A-Za-z]*\([^;]*ImVec4[[:space:]]*\(')"
if [ -n "$inline_style" ]; then
    echo "VIOLATION: PushStyleColor with an inline ImVec4 color literal in editor/ui (use an EditorUi:: palette color)"
    echo "$inline_style"
    echo
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "editor UI color discipline: OK"
fi
exit "$status"
