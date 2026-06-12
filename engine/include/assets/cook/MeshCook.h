#pragma once

#include <assets/cook/AssetImporter.h>
#include <render/static_mesh/StaticMeshData.h>

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

struct ImportedGltfMesh
{
    // glTF mesh name; may be empty (artifact naming falls back to ordinals).
    std::string Name;
    StaticMeshData Data;
};

// Pure stage half: glTF bytes → one validated StaticMeshData per glTF mesh,
// primitives as sections (MaterialSlot = primitive ordinal), geometry in
// mesh-local space (node transforms are the scene's business, not the
// cook's). Errors travel in `error`.
[[nodiscard]] bool ImportGltfMeshes(std::span<const std::byte> bytes,
                                    std::vector<ImportedGltfMesh>& out,
                                    std::string* error = nullptr);

// MikkTSpace over one section's triangles: de-index, generate, re-weld
// exact-duplicate vertices. Exposed for tests; ImportGltfMeshes calls it for
// primitives that have UVs but no authored tangents.
[[nodiscard]] bool GenerateSectionTangents(std::vector<StaticMeshVertex>& vertices,
                                           std::vector<uint32_t>& indices,
                                           std::string* error = nullptr);

//=============================================================================
// GltfMeshImporter — .glb/.gltf → cooked .smesh artifact(s).
//
// A single-mesh source keeps the source's virtual path (the texture-cook
// precedent: "asset://meshes/chair.glb" serves .smesh bytes). A multi-mesh
// source emits "asset://<source>#<mesh-name>" per mesh — '#' cannot appear
// in scanned file paths, so cooked names can never collide with real files.
//=============================================================================
class GltfMeshImporter final : public IAssetImporter
{
public:
    [[nodiscard]] std::vector<std::string_view> SourceExtensions() const override;
    [[nodiscard]] ImportResult Import(const ImportInput& input,
                                      ICookOutputWriter& output) override;
};
