#pragma once

// Which input devices a presentation surface currently owns: the engine's debug
// overlay, an authored UI screen, or an editor shell. The surface is the
// authority on this -- it is the one that knows whether a text field has focus
// or the pointer is over a panel -- and everything downstream reads it rather
// than guessing.
//
// Two consumers, for two different reasons. An editor's input router drops
// events for a device the UI owns before any viewport tool sees them. And
// anything reading raw device state out of InputFrame, rather than mapped
// actions, gates on it: the canonical device snapshot records every event that
// happened, including the ones typed into a console, so a raw reader needs to be
// told when those keystrokes were not meant for it. Action-driven consumers need
// none of this -- an InputContextLease already answers it.
//
// Deliberately free of any UI-backend dependency: it is the small, stable
// vocabulary those layers share. If a surface changes from Dear ImGui to an
// authored document, only the code producing this struct changes.
struct UiInputCapture
{
    bool Mouse = false;
    bool Keyboard = false;
};
