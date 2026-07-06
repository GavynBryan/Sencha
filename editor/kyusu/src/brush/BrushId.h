#pragma once

#include <core/identity/StrongId.h>

#include <cstdint>

// Stable handle for a brush's mesh in the BrushMeshStore. The BrushComponent
// holds this (trivially copyable, archetype-safe) while the heavy BrushMesh
// lives in the editor-side store. Zero is invalid.
// (docs/plans/sencha-level-editor/03-brush-representation.md §2.2)
using BrushId = StrongId<struct BrushIdTag, std::uint32_t>;
