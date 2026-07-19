// Evidence generator (docs/plans/evidence/lightmap-atlas/): cooks the Phase 0
// lightmap spike scene. A quarter-round beveled wall with authored soft edges
// (the chart-continuity risk), a pillar wall with hard-edged recesses (the
// chart-cut and occlusion risk), one Direct point light inside the curve, and
// one Direct spot raking the pillars. Cooked twice: at the default luxel size
// and at a coarse 0.5 for the side-by-side density judgment.
//
// Skipped unless SENCHA_LIGHTMAP_SPIKE_ROOT names the assets root to cook into.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "brush/BrushMesh.h"
#include "brush/BrushOps.h"

#include <core/assets/AssetRef.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <render/LightComponentTypes.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace
{
    // Marks every manifold edge whose faces meet at less than `maxDegrees`
    // soft, the programmatic stand-in for the SetEdgeSoftness pass an author
    // runs over a beveled strip.
    void SoftenSmoothEdges(BrushMesh& mesh, float maxDegrees)
    {
        std::map<std::array<std::uint32_t, 2>, std::vector<std::uint32_t>> edgeFaces;
        for (std::uint32_t f = 0; f < mesh.Faces.size(); ++f)
        {
            const std::vector<std::uint32_t>& loop = mesh.Faces[f].Loop;
            for (std::size_t i = 0; i < loop.size(); ++i)
                edgeFaces[BrushSoftEdgeKey(loop[i], loop[(i + 1) % loop.size()])].push_back(f);
        }
        const float minCos = std::cos(maxDegrees * 3.14159265f / 180.0f);
        for (const auto& [edge, faces] : edgeFaces)
        {
            if (faces.size() != 2)
                continue;
            const Vec3d a = mesh.Faces[faces[0]].Normal;
            const Vec3d b = mesh.Faces[faces[1]].Normal;
            if (a.Dot(b) >= minCos)
                BrushSetEdgeSoft(mesh, edge[0], edge[1], true);
        }
    }

    // The vertical box edge nearest the given XZ corner, as a vertex pair.
    std::array<std::uint32_t, 2> VerticalEdgeAt(const BrushMesh& mesh, float x, float z)
    {
        std::uint32_t bottom = 0;
        std::uint32_t top = 0;
        float bestBottom = 1e9f;
        float bestTop = 1e9f;
        for (std::uint32_t i = 0; i < mesh.Vertices.size(); ++i)
        {
            const Vec3d p = mesh.Vertices[i].Position;
            const float d = (p.X - x) * (p.X - x) + (p.Z - z) * (p.Z - z);
            if (p.Y < 0.0f)
            {
                if (d < bestBottom) { bestBottom = d; bottom = i; }
            }
            else if (d < bestTop) { bestTop = d; top = i; }
        }
        return { bottom, top };
    }
} // namespace

TEST(LightmapSpike, Generate)
{
    const char* rootEnv = std::getenv("SENCHA_LIGHTMAP_SPIKE_ROOT");
    if (rootEnv == nullptr)
        GTEST_SKIP();
    namespace fs = std::filesystem;
    const fs::path root = rootEnv;

    LoggingProvider logging;
    RegisterDocumentSerializers();

    EditorDocument doc(logging);
    doc.SetDefaultMaterial(
        AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });

    // Floor slab under the whole scene (camera at (0,3,10) looking -Z).
    doc.GetScene().CreateBrush(Vec3d{ 0, -0.25f, 0 }, Vec3d{ 9.0, 0.25, 7.0 });

    // Curved wall: a chunky corner block whose inner vertical edge is beveled
    // into a quarter round (8 planar segments), then smooth edges softened so
    // the arc is one soft-edged run: the chart-continuity test.
    {
        const EntityId corner =
            doc.GetScene().CreateBrush(Vec3d{ -4.5f, 1.5f, -4.0f }, Vec3d{ 2.5, 1.5, 2.5 });
        const BrushMesh* mesh = doc.GetScene().TryGetBrushMesh(corner);
        ASSERT_NE(mesh, nullptr);
        // Local-space corner facing the room (+X, +Z).
        const std::array<std::array<std::uint32_t, 2>, 1> edges{
            VerticalEdgeAt(*mesh, 2.5f, 2.5f) };
        BrushMesh beveled = BrushOps::BevelEdges(
            *mesh, edges, /*width*/ 2.2f, /*segments*/ 8);
        ASSERT_GT(beveled.Faces.size(), mesh->Faces.size());
        SoftenSmoothEdges(beveled, 45.0f);
        ASSERT_FALSE(beveled.SoftEdges.empty());
        const Transform3f transform{ Vec3d{ -4.5f, 1.5f, -4.0f },
                                     Quat<float>::Identity(), Vec3d{ 1, 1, 1 } };
        doc.GetScene().CreateBrushFromMesh(transform, std::move(beveled));
        doc.GetScene().DestroyEntity(corner);
    }

    // Pillar wall: a flat back wall with two proud pillars forming hard-edged
    // recesses; every edge stays hard (one chart per face, the Quake shape).
    doc.GetScene().CreateBrush(Vec3d{ 4.0f, 1.5f, -5.5f }, Vec3d{ 4.0, 1.5, 0.25 });
    doc.GetScene().CreateBrush(Vec3d{ 2.0f, 1.5f, -4.9f }, Vec3d{ 0.4, 1.5, 0.4 });
    doc.GetScene().CreateBrush(Vec3d{ 5.0f, 1.5f, -4.9f }, Vec3d{ 0.4, 1.5, 0.4 });

    // Direct point light inside the curve: smooth falloff across the arc.
    {
        const EntityId entity = doc.GetScene().CreateEntity(Vec3d{ -2.0f, 1.6f, -1.0f });
        PointLightComponent light{};
        light.Color = Vec3d{ 1.0f, 0.82f, 0.55f };
        light.Intensity = 4.0f;
        light.Range = 6.0f;
        light.BakeContribution = LightBakeContribution::Direct;
        doc.GetScene().GetRegistry().Components.AddComponent(entity, light);
    }

    // Direct spot raking the pillar wall (identity rotation aims -Z): crisp
    // occlusion shadows behind the pillars at luxel fidelity.
    {
        const EntityId entity = doc.GetScene().CreateEntity(Vec3d{ 3.5f, 2.2f, -0.5f });
        SpotLightComponent light{};
        light.Color = Vec3d{ 0.55f, 0.75f, 1.0f };
        light.Intensity = 6.0f;
        light.Range = 8.0f;
        light.InnerAngleDegrees = 30.0f;
        light.OuterAngleDegrees = 45.0f;
        light.BakeContribution = LightBakeContribution::Direct;
        doc.GetScene().GetRegistry().Components.AddComponent(entity, light);
    }

    const fs::path levelPath = root / "levels/lightmap_spike.json";
    fs::create_directories(levelPath.parent_path());
    ASSERT_TRUE(doc.SaveAs(levelPath.generic_string()));

    LightmapCookParams fine{};
    const DocumentCookResult fineResult =
        CookDocument(levelPath, root, /*cellSize*/ 64.0, nullptr, nullptr, fine);
    ASSERT_TRUE(fineResult.Success) << fineResult.Error;
    EXPECT_EQ(fineResult.DirectLightCount, 2u);
    EXPECT_GT(fineResult.LightmapAtlasWidth, 0u);

    // Coarse variant for the density side-by-side, cooked as its own level.
    EditorDocument coarseDoc(logging);
    ASSERT_TRUE(coarseDoc.Load(levelPath.generic_string()));
    const fs::path coarsePath = root / "levels/lightmap_spike_coarse.json";
    ASSERT_TRUE(coarseDoc.SaveAs(coarsePath.generic_string()));
    LightmapCookParams coarse{};
    coarse.LuxelSize = 0.5f;
    const DocumentCookResult coarseResult =
        CookDocument(coarsePath, root, /*cellSize*/ 64.0, nullptr, nullptr, coarse);
    ASSERT_TRUE(coarseResult.Success) << coarseResult.Error;
    EXPECT_EQ(coarseResult.DirectLightCount, 2u);
}
