#pragma once

#include <core/assets/AssetRef.h>
#include <render/static_mesh/MeshGeometry.h>

#include <string>
#include <vector>

struct BrushMesh;

// Bakes ONE brush's polygon mesh to renderable static-mesh geometry, in the
// brush's LOCAL space (identity transform into the shared tessellation): the
// entity's transform keeps applying through the scene, so a baked mesh can be
// moved, rotated, and instanced like any placed asset. Faces group into one
// section per distinct material (empty face material resolves to levelDefault);
// outMaterialOrder lists them in section MaterialSlot order. Uses the same
// tessellate + bake kernels as the level cook, so the baked mesh is exactly
// what the editor previewed. False (with *error set) when the brush yields no
// geometry.
[[nodiscard]] bool BakeBrushToGeometry(const BrushMesh& mesh,
                                       const AssetRef& levelDefault,
                                       MeshGeometry& out,
                                       std::vector<AssetRef>& outMaterialOrder,
                                       std::string* error);
