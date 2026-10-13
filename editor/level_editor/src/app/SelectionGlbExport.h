#pragma once

class EditorWorkspace;
class SdlWindow;

// Writes the selected brush's geometry as binary glTF where a save dialog over
// `window` says; the geometry is baked before the dialog opens, so its answer
// touches no editor state.
void ExportSelectionGlb(SdlWindow& window, EditorWorkspace& workspace);
