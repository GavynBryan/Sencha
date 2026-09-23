#pragma once

#include <anim/AnimationClip.h>
#include <anim/Skeleton.h>
#include <assets/cook/AssetImporter.h>
#include <assets/skinned_mesh/SkinnedMeshData.h>
#include <assets/static_mesh/MeshGeometry.h>

#include <cstdint>
#include <optional>
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

struct ImportedGltfMesh
{
    // The artifact identity before sanitizing: a static mesh is named by the
    // node that places it, a skinned mesh by its glTF mesh, and an unnamed one
    // by its glTF index ("node12", "mesh3"). `Origin` describes the source
    // element for diagnostics.
    std::string Name;
    std::string Origin;

    // Model space: a static mesh is baked through its node's world transform,
    // a skinned mesh sits in its skeleton's model space.
    MeshGeometry Geometry;

    // Index of the glTF skin this mesh is skinned to, or -1 if static. When
    // set, Skinning is populated with skeleton-local joints and normalized
    // weights, but Skinning->SkeletonPath is left empty — the importer
    // assigns it once skeleton artifact paths are decided, then emits the
    // mesh as a SkinnedMeshData (`.skmesh`) rather than a `.smesh`.
    int SkinIndex = -1;
    std::optional<MeshSkinning> Skinning;
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

// Everything one glTF source yields (Decisions B, J, M): meshes, skeletons
// (one per skin), and animation clips. Skeleton-local joint resolution,
// weight normalization, and node→joint remapping all happen here so the
// runtime never fixes data (Decision N). SkeletonPath fields are left empty
// for the importer to fill from artifact naming.
struct ImportedGltfScene
{
    std::vector<ImportedSkeleton> Skeletons;
    std::vector<ImportedGltfMesh> Meshes;
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
// serves .smesh bytes). Otherwise every mesh is "asset://<source>#<name>" —
// '#' cannot appear in scanned file paths, so cooked names can never collide
// with real files. Skeletons and animations always take the '#'-suffixed form
// ("asset://<source>#skel:<name>", "asset://<source>#anim:<name>"), and the
// skinned mesh / clip artifacts reference the skeleton artifact by that path.
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
