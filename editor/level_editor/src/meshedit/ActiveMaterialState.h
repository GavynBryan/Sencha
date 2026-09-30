#pragma once

#include <core/assets/AssetRef.h>

#include <array>

// The editor-wide material the texturing verbs act with: the browser and
// viewport sampling write Active; the apply verbs read it. Slots stash
// materials for one-click recall (clicking a slot swaps it with Active).
// View state, not document state: never serialized with the level, never an
// undo step.
struct ActiveMaterialState
{
    AssetRef Active;               // empty until the first pick or sample
    std::array<AssetRef, 3> Slots; // clicking an empty slot stores Active there
};
