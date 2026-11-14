#pragma once

#include <anim/AnimationClip.h>
#include <anim/Skeleton.h>
#include <assets/cook/AssetImporter.h>
#include <assets/skinned_mesh/SkinnedMeshData.h>
#include <assets/static_mesh/MeshGeometry.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// Mesh cook (docs/assets/pipeline.md, Decisions B, M). Dev-only — compiled
// under SENCHA_ENABLE_COOK, never shipped.
//
// glTF 2.0 is the one mesh import path: .blend funnels through it via
// headless Blender (BlendCook.h). A source is a .glb, or a .gltf whose
// buffers are embedded data: URIs or files beside it. External buffers are
// read through ImportInput::Sources and listed in AdditionalSources, so the
// cooked cache recooks when a .bin changes without its .gltf (Decision B).
// Image URIs are never read: materials are authored .smat.
//
// Every cooked mesh carries Decision M tangents: authored TANGENT streams
// are taken as-is (w snapped to ±1 — normalization is the cook's job, the
// runtime never fixes data), sources with UVs get MikkTSpace tangents
// (skinned ones too: influences ride the de-index and reweld with their
// vertices), and UV-less sources get a deterministic normal-derived basis so
// the format invariant (tangent w is ±1) holds for every vertex.
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

// Answers a .gltf's external buffer: `uri` is the buffer's URI as authored,
// percent-decoded, relative to the .gltf. False when it cannot be read.
using GltfBufferReader = std::function<bool(std::string_view uri, std::vector<std::byte>& out)>;

// Pure stage half: one parse → the full scene. Errors travel in `error`.
// Without `readBuffer`, a source with external buffers is refused.
[[nodiscard]] bool ImportGltfScene(std::span<const std::byte> bytes,
                                   ImportedGltfScene& out,
                                   std::string* error = nullptr,
                                   const GltfBufferReader& readBuffer = {});

// MikkTSpace over one section's triangles: de-index, generate, re-weld
// exact-duplicate vertices. Exposed for tests; the import calls it for
// primitives that have UVs but no authored tangents. `influences`, when
// given, is per vertex and is carried through with its vertex, so a corner
// welds only with one that agrees on both.
[[nodiscard]] bool GenerateSectionTangents(std::vector<StaticMeshVertex>& vertices,
                                           std::vector<uint32_t>& indices,
                                           std::string* error = nullptr,
                                           std::vector<MeshSkinInfluence>* influences = nullptr);

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
