#include "document/BrushBake.h"

#include "brush/BrushMesh.h"
#include "brush/BrushOps.h"
#include "export/GltfMeshExport.h"

#include <assets/cook/MeshCook.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
BrushMesh TwoMaterialBox()
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    box.Faces[0].Material.Material = AssetRef{ AssetType::Material, "asset://materials/a.smat" };
    box.Faces[1].Material.Material = AssetRef{ AssetType::Material, "asset://materials/b.smat" };
    return box;
}
}

TEST(BrushBake, BakesLocalSpaceGeometryWithSectionsPerMaterial)
{
    const BrushMesh box = TwoMaterialBox();
    const AssetRef levelDefault{ AssetType::Material, "asset://materials/default.smat" };

    MeshGeometry geometry;
    std::vector<AssetRef> materials;
    std::string error;
    ASSERT_TRUE(BakeBrushToGeometry(box, levelDefault, geometry, materials, &error)) << error;

    // Three distinct materials: a, b, and the level default for the other faces.
    ASSERT_EQ(materials.size(), 3u);
    EXPECT_EQ(geometry.Sections.size(), 3u);
    EXPECT_FALSE(geometry.Vertices.empty());
    EXPECT_FALSE(geometry.Indices.empty());

    // Local-space bake: the unit box's bounds are its own half extents (the
    // entity transform is applied by the scene, not baked into the vertices).
    EXPECT_NEAR(geometry.LocalBounds.Min.X, -1.0f, 1e-4f);
    EXPECT_NEAR(geometry.LocalBounds.Max.X, 1.0f, 1e-4f);

    // Every vertex carries a generated tangent (w = +/-1).
    for (const StaticMeshVertex& v : geometry.Vertices)
        EXPECT_NEAR(std::abs(v.Tangent.W), 1.0f, 1e-4f);
}

TEST(BrushBake, EmitsLightmapSheetUvsWithDisjointFaceRects)
{
    // A baked (instanceable) mesh carries a [0,1] lightmap sheet: per-face
    // charts on a hard-edged box must land in disjoint sheet rects, so no
    // two faces of the box share the same UV rectangle.
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    MeshGeometry geometry;
    std::vector<AssetRef> materials;
    std::string error;
    ASSERT_TRUE(BakeBrushToGeometry(
        box, AssetRef{ AssetType::Material, "asset://materials/d.smat" },
        geometry, materials, &error)) << error;

    bool anyNonzero = false;
    for (const StaticMeshVertex& v : geometry.Vertices)
        anyNonzero = anyNonzero || v.LightmapU != 0 || v.LightmapV != 0;
    EXPECT_TRUE(anyNonzero);

    // Group vertices by face normal axis (a box face = one chart) and check
    // the six UV bounding boxes are pairwise disjoint.
    struct Rect { float MinU = 2, MinV = 2, MaxU = -1, MaxV = -1; };
    Rect rects[6];
    for (const StaticMeshVertex& v : geometry.Vertices)
    {
        int axis = 0;
        float best = 0.0f;
        const float components[3] = { v.Normal.X, v.Normal.Y, v.Normal.Z };
        for (int i = 0; i < 3; ++i)
            if (std::abs(components[i]) > best)
            {
                best = std::abs(components[i]);
                axis = i * 2 + (components[i] < 0 ? 1 : 0);
            }
        Rect& rect = rects[axis];
        const float u = v.LightmapU / 65535.0f;
        const float uvV = v.LightmapV / 65535.0f;
        rect.MinU = std::min(rect.MinU, u);
        rect.MinV = std::min(rect.MinV, uvV);
        rect.MaxU = std::max(rect.MaxU, u);
        rect.MaxV = std::max(rect.MaxV, uvV);
    }
    for (int a = 0; a < 6; ++a)
        for (int b = a + 1; b < 6; ++b)
        {
            const bool overlaps = rects[a].MinU < rects[b].MaxU
                && rects[b].MinU < rects[a].MaxU
                && rects[a].MinV < rects[b].MaxV
                && rects[b].MinV < rects[a].MaxV;
            EXPECT_FALSE(overlaps) << "faces " << a << " and " << b;
        }
}

TEST(BrushBake, EmptyBrushFailsWithError)
{
    BrushMesh empty;
    MeshGeometry geometry;
    std::vector<AssetRef> materials;
    std::string error;
    EXPECT_FALSE(BakeBrushToGeometry(empty, AssetRef{}, geometry, materials, &error));
    EXPECT_FALSE(error.empty());
}

TEST(GltfMeshExport, GlbRoundTripsThroughTheImporter)
{
    const BrushMesh box = TwoMaterialBox();
    MeshGeometry baked;
    std::vector<AssetRef> materials;
    std::string error;
    ASSERT_TRUE(BakeBrushToGeometry(box, AssetRef{ AssetType::Material, "asset://materials/d.smat" },
                                    baked, materials, &error)) << error;

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "sencha_bake_roundtrip_test.glb";
    ASSERT_TRUE(WriteGlbFile(baked, materials, path, &error)) << error;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(file.is_open());
    std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();

    std::vector<ImportedGltfMesh> imported;
    ASSERT_TRUE(ImportGltfMeshes(bytes, imported, &error)) << error;
    ASSERT_EQ(imported.size(), 1u);

    const MeshGeometry& in = imported[0].Geometry;
    EXPECT_EQ(in.Sections.size(), baked.Sections.size());
    EXPECT_EQ(in.Indices.size(), baked.Indices.size());
    // Positions survive exactly (same handedness and axes on both sides): the
    // bounds of the reimported mesh match the baked ones.
    EXPECT_NEAR(in.LocalBounds.Min.X, baked.LocalBounds.Min.X, 1e-4f);
    EXPECT_NEAR(in.LocalBounds.Max.Y, baked.LocalBounds.Max.Y, 1e-4f);
    EXPECT_NEAR(in.LocalBounds.Max.Z, baked.LocalBounds.Max.Z, 1e-4f);

    std::filesystem::remove(path);
}
