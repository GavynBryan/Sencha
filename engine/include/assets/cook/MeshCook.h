#pragma once

#include <anim/AnimationClip.h>
#include <anim/Skeleton.h>
#include <assets/cook/AssetImporter.h>
#include <assets/skinned_mesh/SkinnedMeshData.h>
#include <assets/static_mesh/MeshGeometry.h>

#include <cstdint>
#include <string>
#include <vector>

//=============================================================================
// Mesh cook (docs/assets/pipeline.md, Decisions B, M). Dev-only — compiled
// under SENCHA_ENABLE_COOK, never shipped.
//
// glTF 2.0 is the one mesh import path: .blend funnels through it via
// headless Blender (BlendCook.h). Sources must be self-contained — .glb, or
// .gltf with embedded data: URIs. External buffer files are rejected: the
// cooked cache keys staleness on one source file's hash (Decision B), and a
// sibling .bin that can change without the .gltf changing would rot it.
//
// Every cooked mesh carries Decision M tangents: authored TANGENT streams
// are taken as-is (w snapped to ±1 — normalization is the cook's job, the
// runtime never fixes data), sources with UVs get MikkTSpace tangents, and
// UV-less sources get a deterministic normal-derived basis so the format
// invariant (tangent w is ±1) holds for every vertex.
//
// Every artifact is in the engine frame (GltfFrame.h): the importer bakes the
// half turn from glTF's +Z front to the engine's -Z forward into geometry and
// folds it into skeletons, and nothing downstream knows about it.
//=============================================================================

// A static mesh: one per node that places a mesh without a skin or a joint
// above it, baked through the node's world transform.
struct ImportedGltfMesh
{
    // The artifact identity before sanitizing: the placing node's name, or
    // "node<index>" when unnamed. `Origin` describes the node for diagnostics.
    std::string Name;
    std::string Origin;
    MeshGeometry Geometry;
};

// Everything one skeleton draws, as one mesh in the skeleton's model space:
// every skinned mesh placed with its skin, and every mesh parented beneath one
// of its joints as a rigid part bound wholly to that joint. Each piece is baked
// into model space before it joins, and the pieces are grouped into one
// section per material, in first-appearance order.
struct ImportedSkinnedModel
{
    // The skeleton's name; the model and its skeleton are one identity.
    std::string Name;
    std::string Origin;
    int SkinIndex = -1;
    MeshGeometry Geometry;

    // Skeleton-local joints and normalized weights. SkeletonPath is left
    // empty for the importer to assign from artifact naming.
    MeshSkinning Skinning;
};

struct ImportedSkeleton
{
    // The glTF skin name, or "skin<index>" when unnamed.
    std::string Name;
    std::string Origin;
    SkeletonData Data;
};

struct ImportedAnimation
{
    // The glTF animation name, or "animation<index>" when unnamed.
    std::string Name;
    std::string Origin;
    AnimationClipData Data; // SkeletonPath left empty; the importer assigns it.

    // The glTF skin this animation poses (its channels target that skin's
    // joints), or -1 if it targets no skin's joints (skipped by the importer).
    int SkinIndex = -1;
};

// Everything one glTF source yields (Decisions B, J, M): skeletons (one per
// skin, indexed by skin), a model for each skeleton that draws anything, static
// meshes, and animation clips. Skeleton-local joint resolution, weight
// normalization, and node→joint remapping all happen here so the runtime never
// fixes data (Decision N). SkeletonPath fields are left empty for the importer
// to fill from artifact naming.
struct ImportedGltfScene
{
    std::vector<ImportedSkeleton> Skeletons;
    std::vector<ImportedSkinnedModel> SkinnedModels;
    std::vector<ImportedGltfMesh> StaticMeshes;
    std::vector<ImportedAnimation> Animations;
};

// Pure stage half: one parse → the full scene. Errors travel in `error`.
[[nodiscard]] bool ImportGltfScene(std::span<const std::byte> bytes,
                                   ImportedGltfScene& out,
                                   std::string* error = nullptr);

// MikkTSpace over one section's triangles: de-index, generate, re-weld
// exact-duplicate vertices. Exposed for tests; the import calls it for
// primitives that have UVs but no authored tangents.
[[nodiscard]] bool GenerateSectionTangents(std::vector<StaticMeshVertex>& vertices,
                                           std::vector<uint32_t>& indices,
                                           std::string* error = nullptr);

//=============================================================================
// GltfMeshImporter — .glb/.gltf → cooked .smesh + .sskel + .sanim artifacts.
//
// A source that places a single static mesh and has no skins keeps the
// source's virtual path (the texture-cook precedent: "asset://meshes/chair.glb"
// serves .smesh bytes). Otherwise every static mesh is
// "asset://<source>#<node-name>" — '#' cannot appear in scanned file paths, so
// cooked names can never collide with real files. A skin yields
// "asset://<source>#skel:<skin>" and, when it draws anything, the model
// "asset://<source>#model:<skin>"; clips are "asset://<source>#anim:<name>".
// The model and the clips reference the skeleton artifact by its path.
//
// Names are the source's own, sanitized to [A-Za-z0-9_-], with an index for
// an unnamed element. Two elements that land on one name fail the import with
// both named; a name never depends on discovery order.
//=============================================================================
class GltfMeshImporter final : public IAssetImporter
{
public:
    [[nodiscard]] std::vector<std::string_view> SourceExtensions() const override;
    [[nodiscard]] ImportResult Import(const ImportInput& input,
                                      ICookOutputWriter& output) override;
    [[nodiscard]] std::uint64_t CookIdentity() const override;
};
