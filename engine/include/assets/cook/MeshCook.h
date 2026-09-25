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
//=============================================================================

// Placement, naming and engine-frame rules: docs/assets/pipeline.md, "The glTF
// import contract". Each element's `Origin` names its source element in errors.
struct ImportedGltfMesh
{
    // The placing node's name, or "node<index>" when unnamed.
    std::string Name;
    std::string Origin;
    MeshGeometry Geometry;
};

// Everything one skeleton draws, in the skeleton's model space.
struct ImportedSkinnedModel
{
    // The skeleton's name; the model and its skeleton are one identity.
    std::string Name;
    std::string Origin;
    int SkinIndex = -1;
    MeshGeometry Geometry;
    MeshSkinning Skinning; // SkeletonPath left empty; the importer assigns it.
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

// Everything one glTF source yields; Skeletons is indexed by skin. Joint
// resolution, weight normalization and node→joint remapping all happen here so
// the runtime never fixes data (Decision N).
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

// .glb/.gltf → cooked .smesh, .skmesh, .sskel and .sanim artifacts, named as
// docs/assets/pipeline.md's glTF import contract tabulates.
class GltfMeshImporter final : public IAssetImporter
{
public:
    [[nodiscard]] std::vector<std::string_view> SourceExtensions() const override;
    [[nodiscard]] ImportResult Import(const ImportInput& input,
                                      ICookOutputWriter& output) override;
    [[nodiscard]] std::uint64_t CookIdentity() const override;
};
