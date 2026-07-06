#pragma once

#include "BrushMesh.h"
#include "BrushMeshStore.h"

#include <core/json/JsonValue.h>

//=============================================================================
// BrushMeshSerialization — pure JSON <-> BrushMesh conversion for the brush
// sidecar (03-§5, 04-§4). Lives in the brush kernel (no scene/UI deps) so it
// is unit-testable on its own; EditorDocument only embeds the result.
//
// A face serializes as { loop, material?, uv } with the UV stored as a
// PROJECTION (axes/scale/offset/rotation), never baked coordinates — that is
// what lets UVs survive resize. A bare-array face (the pre-texturing form) and a
// face with no "material" load cleanly (empty material => level default).
//=============================================================================

[[nodiscard]] JsonValue BrushMeshToJson(const BrushMesh& mesh);
[[nodiscard]] BrushMesh BrushMeshFromJson(const JsonValue& value);

// The whole store as an object keyed by BrushId text. Load validates/repairs
// each mesh so a corrupt brush is never accepted silently.
[[nodiscard]] JsonValue SerializeBrushMeshes(const BrushMeshStore& store);
void DeserializeBrushMeshes(const JsonValue& value, BrushMeshStore& store);
