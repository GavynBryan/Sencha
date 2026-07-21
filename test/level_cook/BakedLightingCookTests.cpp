// End-to-end: a brush level with a LightBakeContribution::Direct light cooks
// to a per-zone lightmap atlas (.stex artifact + ZoneLightmap scene entity +
// vertex lightmap UVs), a None light leaves no atlas, and every bake input
// that changes the lightmap restales the cook hash. Headless: no AssetSystem,
// no graphics.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "brush/BrushMesh.h"

#include <assets/static_mesh/MeshLoader.h>
#include <assets/texture/TextureLoader.h>
#include <core/assets/AssetRef.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <render/LightComponentTypes.h>
#include <render/PointLightComponent.h>
#include <render/static_mesh/MeshGeometry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
namespace fs = std::filesystem;

class BakedLightingCookTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = fs::temp_directory_path()
            / ("sencha_bakelight_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(Root);
        fs::create_directories(Root);
        const fs::path material = Root / "materials/dev/gray.smat";
        fs::create_directories(material.parent_path());
        std::ofstream(material, std::ios::trunc) << "{}";
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(Root, ec);
    }

    // A floor slab at the origin plus a point light above it, authored with the
    // given bake contribution. Returns the saved level path.
    fs::path AuthorFloorWithLight(LightBakeContribution contribution,
                                  Vec3d lightPosition = Vec3d{ 0, 4, 0 })
    {
        EditorDocument doc(Logging);
        doc.SetDefaultMaterial(
            AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
        doc.GetScene().CreateBrush(Vec3d{ 0, 0, 0 }, Vec3d{ 4.0, 0.25, 4.0 });

        const EntityId lightEntity = doc.GetScene().CreateEntity(lightPosition);
        PointLightComponent light{};
        light.Color = Vec3d(1.0f, 1.0f, 1.0f);
        light.Intensity = 40.0f;
        light.Range = 12.0f;
        light.BakeContribution = contribution;
        doc.GetScene().GetRegistry().Components.AddComponent(lightEntity, light);

        const fs::path levelPath = Root / "levels/test.json";
        fs::create_directories(levelPath.parent_path());
        EXPECT_TRUE(doc.SaveAs(levelPath.generic_string()));
        return levelPath;
    }

    // Two coplanar quads sharing an edge, plus a Direct light. Toggling the
    // shared edge soft merges the two per-face charts into one without moving
    // a single vertex byte: the staleness case only the chart hash catches.
    fs::path AuthorCoplanarPair(bool sharedEdgeSoft)
    {
        BrushMesh mesh;
        mesh.Vertices = {
            BrushVertex{ Vec3d{ -2, 0, -1 } }, BrushVertex{ Vec3d{ 0, 0, -1 } },
            BrushVertex{ Vec3d{ 2, 0, -1 } },  BrushVertex{ Vec3d{ -2, 0, 1 } },
            BrushVertex{ Vec3d{ 0, 0, 1 } },   BrushVertex{ Vec3d{ 2, 0, 1 } },
        };
        mesh.Faces = {
            BrushFace{ { 0, 1, 4, 3 }, Vec3d{ 0, 1, 0 }, {} },
            BrushFace{ { 1, 2, 5, 4 }, Vec3d{ 0, 1, 0 }, {} },
        };
        if (sharedEdgeSoft)
            BrushSetEdgeSoft(mesh, 1, 4, true);

        EditorDocument doc(Logging);
        doc.SetDefaultMaterial(
            AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
        doc.GetScene().CreateBrushFromMesh(Transform3f{}, std::move(mesh));

        const EntityId lightEntity = doc.GetScene().CreateEntity(Vec3d{ 0, 3, 0 });
        PointLightComponent light{};
        light.Intensity = 20.0f;
        light.Range = 10.0f;
        light.BakeContribution = LightBakeContribution::Direct;
        doc.GetScene().GetRegistry().Components.AddComponent(lightEntity, light);

        const fs::path levelPath = Root / "levels/test.json";
        fs::create_directories(levelPath.parent_path());
        EXPECT_TRUE(doc.SaveAs(levelPath.generic_string()));
        return levelPath;
    }

    fs::path AuthorSplitReceiver(bool oppositeDiagonal, std::string_view stem)
    {
        const auto quad = [oppositeDiagonal](float minX, float maxX,
                                              float minZ, float maxZ)
        {
            BrushMesh mesh;
            mesh.Vertices = {
                BrushVertex{ Vec3d{ minX, 0, minZ } },
                BrushVertex{ Vec3d{ maxX, 0, minZ } },
                BrushVertex{ Vec3d{ maxX, 0, maxZ } },
                BrushVertex{ Vec3d{ minX, 0, maxZ } },
            };
            BrushFace face{ { 0, 3, 2, 1 }, {}, {} }; // +Y receiver
            if (oppositeDiagonal)
                std::rotate(face.Loop.begin(), face.Loop.begin() + 1, face.Loop.end());
            face.Normal = BrushComputeFaceNormal(mesh, face);
            mesh.Faces.push_back(std::move(face));
            return mesh;
        };

        EditorDocument doc(Logging);
        doc.SetDefaultMaterial(
            AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
        // Separate meshes deliberately duplicate the x=3 boundary vertices.
        // Positive coordinates keep this compact fixture in one cooked cell.
        doc.GetScene().CreateBrushFromMesh(
            Transform3f{}, quad(1.0f, 3.0f, -1.0f, 1.0f));
        doc.GetScene().CreateBrushFromMesh(
            Transform3f{}, quad(3.0f, 5.0f, -1.0f, 1.0f));

        // A small open, one-sided wall creates an actual shadow boundary on
        // the receiver and also proves occlusion is independent of its fan.
        BrushMesh blocker;
        blocker.Vertices = {
            BrushVertex{ Vec3d{ 3.75f, 0.05f, -0.2f } },
            BrushVertex{ Vec3d{ 3.75f, 2.0f, -0.2f } },
            BrushVertex{ Vec3d{ 3.75f, 2.0f, 0.2f } },
            BrushVertex{ Vec3d{ 3.75f, 0.05f, 0.2f } },
        };
        BrushFace blockerFace{ { 0, 1, 2, 3 }, {}, {} };
        if (oppositeDiagonal)
            std::rotate(blockerFace.Loop.begin(), blockerFace.Loop.begin() + 1,
                        blockerFace.Loop.end());
        blockerFace.Normal = BrushComputeFaceNormal(blocker, blockerFace);
        blocker.Faces.push_back(std::move(blockerFace));
        doc.GetScene().CreateBrushFromMesh(Transform3f{}, std::move(blocker));

        // High enough that the radiance gradient across the shared boundary
        // stays well within what the default 0.25 luxel grid can resolve; a
        // near-floor hotspot would put multiple radiance doublings inside one
        // luxel and make cross-parameterization comparisons meaningless.
        const EntityId lightEntity = doc.GetScene().CreateEntity(
            Vec3d{ 2.8f, 1.5f, 0.3f });
        PointLightComponent light{};
        light.Intensity = 8.0f;
        light.Range = 6.0f;
        light.BakeContribution = LightBakeContribution::Direct;
        doc.GetScene().GetRegistry().Components.AddComponent(lightEntity, light);

        const fs::path levelPath = Root / "levels" / (std::string(stem) + ".json");
        fs::create_directories(levelPath.parent_path());
        EXPECT_TRUE(doc.SaveAs(levelPath.generic_string()));
        return levelPath;
    }

    // Two coplanar floor quads duplicating the x=3 seam, a one-sided wall
    // standing exactly ON that seam, and a light on the left. The lighting on
    // the floor plane is discontinuous exactly along the seam: lit up to the
    // wall base, hard shadow behind it.
    fs::path AuthorSeamWall(std::string_view stem)
    {
        const auto quad = [](float minX, float maxX, float minZ, float maxZ)
        {
            BrushMesh mesh;
            mesh.Vertices = {
                BrushVertex{ Vec3d{ minX, 0, minZ } },
                BrushVertex{ Vec3d{ maxX, 0, minZ } },
                BrushVertex{ Vec3d{ maxX, 0, maxZ } },
                BrushVertex{ Vec3d{ minX, 0, maxZ } },
            };
            BrushFace face{ { 0, 3, 2, 1 }, {}, {} }; // +Y receiver
            face.Normal = BrushComputeFaceNormal(mesh, face);
            mesh.Faces.push_back(std::move(face));
            return mesh;
        };

        EditorDocument doc(Logging);
        doc.SetDefaultMaterial(
            AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
        doc.GetScene().CreateBrushFromMesh(
            Transform3f{}, quad(1.0f, 3.0f, -1.0f, 1.0f));
        doc.GetScene().CreateBrushFromMesh(
            Transform3f{}, quad(3.0f, 5.0f, -1.0f, 1.0f));

        BrushMesh wall;
        wall.Vertices = {
            BrushVertex{ Vec3d{ 3.0f, 0.0f, -1.0f } },
            BrushVertex{ Vec3d{ 3.0f, 2.0f, -1.0f } },
            BrushVertex{ Vec3d{ 3.0f, 2.0f, 1.0f } },
            BrushVertex{ Vec3d{ 3.0f, 0.0f, 1.0f } },
        };
        BrushFace wallFace{ { 0, 1, 2, 3 }, {}, {} };
        wallFace.Normal = BrushComputeFaceNormal(wall, wallFace);
        wall.Faces.push_back(std::move(wallFace));
        doc.GetScene().CreateBrushFromMesh(Transform3f{}, std::move(wall));

        const EntityId lightEntity = doc.GetScene().CreateEntity(
            Vec3d{ 1.5f, 1.0f, 0.0f });
        PointLightComponent light{};
        light.Intensity = 8.0f;
        light.Range = 8.0f;
        light.BakeContribution = LightBakeContribution::Direct;
        doc.GetScene().GetRegistry().Components.AddComponent(lightEntity, light);

        const fs::path levelPath = Root / "levels" / (std::string(stem) + ".json");
        fs::create_directories(levelPath.parent_path());
        EXPECT_TRUE(doc.SaveAs(levelPath.generic_string()));
        return levelPath;
    }

    std::vector<std::byte> ReadBytes(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::vector<char> chars((std::istreambuf_iterator<char>(file)), {});
        std::vector<std::byte> bytes(chars.size());
        for (std::size_t i = 0; i < chars.size(); ++i)
            bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(chars[i]));
        return bytes;
    }

    Vec3d SampleLightmap(const TextureData& texture, Vec2d uv)
    {
        const float px = uv.X * texture.Width - 0.5f;
        const float py = uv.Y * texture.Height - 0.5f;
        const int x0 = static_cast<int>(std::floor(px));
        const int y0 = static_cast<int>(std::floor(py));
        const float tx = px - std::floor(px);
        const float ty = py - std::floor(py);
        const auto at = [&](int x, int y)
        {
            x = (x % static_cast<int>(texture.Width) + texture.Width) % texture.Width;
            y = (y % static_cast<int>(texture.Height) + texture.Height) % texture.Height;
            const std::size_t offset =
                (static_cast<std::size_t>(y) * texture.Width + x) * 4;
            std::uint32_t packed = 0;
            std::memcpy(&packed, texture.Blob.data() + offset, sizeof(packed));
            // RGB9E5 texel decode, the sampler's pre-filter conversion.
            const float scale = std::exp2(static_cast<float>(
                static_cast<int>(packed >> 27) - 15 - 9));
            return Vec3d{ static_cast<float>(packed & 0x1FFu) * scale,
                          static_cast<float>((packed >> 9) & 0x1FFu) * scale,
                          static_cast<float>((packed >> 18) & 0x1FFu) * scale };
        };
        const Vec3d top = at(x0, y0) * (1.0f - tx) + at(x0 + 1, y0) * tx;
        const Vec3d bottom = at(x0, y0 + 1) * (1.0f - tx)
            + at(x0 + 1, y0 + 1) * tx;
        return top * (1.0f - ty) + bottom * ty;
    }

    // Atlas radiance at the midpoint of the duplicated x=3, z=[-1,1] floor
    // seam, sampled through each side's independently packed
    // parameterization: [0] the x<3 chart, [1] the x>3 chart.
    std::array<Vec3d, 2> SeamMidpointRadiance(const fs::path& cookedRoot)
    {
        TextureData texture;
        std::string textureError;
        const std::vector<std::byte> textureBytes =
            ReadBytes(cookedRoot / "lightmap.stex");
        EXPECT_TRUE(LoadStexFromBytes(textureBytes, texture, &textureError))
            << textureError;

        MeshGeometry geometry;
        MeshLoader loader(Logging);
        bool loadedMesh = false;
        for (const fs::directory_entry& entry : fs::directory_iterator(cookedRoot))
            if (entry.path().extension() == ".smesh")
            {
                EXPECT_FALSE(loadedMesh) << "fixture should cook into one cell";
                if (loader.LoadFromFile(entry.path().generic_string(), geometry))
                    loadedMesh = true;
            }
        EXPECT_TRUE(loadedMesh);

        std::array<Vec3d, 2> radiance{};
        std::array<bool, 2> found{};
        for (std::size_t i = 0; i + 2 < geometry.Indices.size(); i += 3)
            for (int edge = 0; edge < 3; ++edge)
            {
                const StaticMeshVertex& a =
                    geometry.Vertices[geometry.Indices[i + edge]];
                const StaticMeshVertex& b =
                    geometry.Vertices[geometry.Indices[i + (edge + 1) % 3]];
                const StaticMeshVertex& opposite =
                    geometry.Vertices[geometry.Indices[i + (edge + 2) % 3]];
                // The opposite-vertex check keeps this to the floor charts: a
                // wall standing on the seam duplicates the same base edge.
                const bool shared = std::abs(a.Position.X - 3.0f) < 1e-6f
                    && std::abs(b.Position.X - 3.0f) < 1e-6f
                    && std::abs(a.Position.Y) < 1e-6f
                    && std::abs(b.Position.Y) < 1e-6f
                    && std::abs(opposite.Position.Y) < 1e-6f
                    && std::abs(std::abs(a.Position.Z - b.Position.Z) - 2.0f) < 1e-6f;
                if (!shared)
                    continue;
                const std::size_t side = opposite.Position.X < 3.0f ? 0 : 1;
                EXPECT_FALSE(found[side]);
                found[side] = true;
                radiance[side] = SampleLightmap(texture, Vec2d{
                    (a.LightmapU + b.LightmapU) * (0.5f / 65535.0f),
                    (a.LightmapV + b.LightmapV) * (0.5f / 65535.0f) });
            }
        EXPECT_TRUE(found[0]);
        EXPECT_TRUE(found[1]);
        return radiance;
    }

    bool AnyCellVertexHasLightmapUv()
    {
        MeshLoader loader(Logging);
        for (const fs::directory_entry& entry :
             fs::recursive_directory_iterator(Root / ".cooked/levels/test"))
        {
            if (entry.path().extension() != ".smesh")
                continue;
            MeshGeometry geometry;
            EXPECT_TRUE(loader.LoadFromFile(entry.path().generic_string(), geometry));
            for (const StaticMeshVertex& vertex : geometry.Vertices)
                if (vertex.LightmapU != 0 || vertex.LightmapV != 0)
                    return true;
        }
        return false;
    }

    bool CookedSceneNamesZoneLightmap()
    {
        std::ifstream file(Root / ".cooked/levels/test.cooked.json");
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str().find("ZoneLightmap") != std::string::npos;
    }

    fs::path Root;
    LoggingProvider Logging;
};
} // namespace

TEST_F(BakedLightingCookTest, DirectLightBakesAtlasAndVertexUvs)
{
    const fs::path levelPath = AuthorFloorWithLight(LightBakeContribution::Direct);
    const DocumentCookResult result = CookDocument(levelPath, Root, /*cellSize*/ 16.0);
    ASSERT_TRUE(result.Success) << result.Error;
    ASSERT_FALSE(result.GeneratedMeshPaths.empty());

    EXPECT_EQ(result.DirectLightCount, 1u);
    EXPECT_GT(result.LightmapAtlasWidth, 0u);
    EXPECT_GT(result.LightmapAtlasHeight, 0u);
    EXPECT_TRUE(fs::exists(Root / ".cooked/levels/test/lightmap.stex"));
    EXPECT_TRUE(CookedSceneNamesZoneLightmap());
    EXPECT_TRUE(AnyCellVertexHasLightmapUv());
}

TEST_F(BakedLightingCookTest, NonBakedLightLeavesNoAtlas)
{
    const fs::path levelPath = AuthorFloorWithLight(LightBakeContribution::None);
    const DocumentCookResult result = CookDocument(levelPath, Root, /*cellSize*/ 16.0);
    ASSERT_TRUE(result.Success) << result.Error;

    EXPECT_EQ(result.DirectLightCount, 0u);
    EXPECT_EQ(result.LightmapAtlasWidth, 0u);
    EXPECT_FALSE(fs::exists(Root / ".cooked/levels/test/lightmap.stex"));
    EXPECT_FALSE(CookedSceneNamesZoneLightmap());
    EXPECT_FALSE(AnyCellVertexHasLightmapUv());
}

TEST_F(BakedLightingCookTest, AoPlaneBakesBesideTheAtlas)
{
    // The seam-wall fixture stands a wall on the floor: its base darkens the
    // nearby floor texels while open floor stays fully visible.
    const DocumentCookResult result =
        CookDocument(AuthorSeamWall("test"), Root, 16.0);
    ASSERT_TRUE(result.Success) << result.Error;

    TextureData ao;
    std::string error;
    ASSERT_TRUE(LoadStexFromBytes(
        ReadBytes(Root / ".cooked/levels/test/ao.stex"), ao, &error)) << error;
    EXPECT_EQ(ao.Format, TexturePixelFormat::R8);
    EXPECT_EQ(ao.Width, result.LightmapAtlasWidth);
    EXPECT_EQ(ao.Height, result.LightmapAtlasHeight);
    ASSERT_EQ(ao.Blob.size(),
              static_cast<std::size_t>(ao.Width) * ao.Height);

    std::uint8_t darkest = 255;
    std::size_t fullyOpen = 0;
    for (const std::uint8_t texel : ao.Blob)
    {
        darkest = std::min(darkest, texel);
        if (texel == 255)
            ++fullyOpen;
    }
    EXPECT_LT(darkest, 200u);   // contact darkening near the wall base
    EXPECT_GT(fullyOpen, 0u);   // open floor and untouched fill stay white

    std::ifstream scene(Root / ".cooked/levels/test.cooked.json");
    std::ostringstream buffer;
    buffer << scene.rdbuf();
    EXPECT_NE(buffer.str().find("\"ao\""), std::string::npos);
}

TEST_F(BakedLightingCookTest, AoDisabledCooksNoPlane)
{
    LightingCookParams noAo{};
    noAo.Ao.Enabled = false;
    const DocumentCookResult result = CookDocument(
        AuthorFloorWithLight(LightBakeContribution::Direct), Root, 16.0,
        nullptr, nullptr, noAo);
    ASSERT_TRUE(result.Success) << result.Error;

    EXPECT_TRUE(fs::exists(Root / ".cooked/levels/test/lightmap.stex"));
    EXPECT_FALSE(fs::exists(Root / ".cooked/levels/test/ao.stex"));

    std::ifstream scene(Root / ".cooked/levels/test.cooked.json");
    std::ostringstream buffer;
    buffer << scene.rdbuf();
    EXPECT_EQ(buffer.str().find("\"ao\""), std::string::npos);
}

TEST_F(BakedLightingCookTest, RestalesOnAoTuningOnlyWhenLightsExist)
{
    LightingCookParams base{};
    LightingCookParams widerReach{};
    widerReach.Ao.MaxDistance = 2.0f;
    LightingCookParams disabled{};
    disabled.Ao.Enabled = false;

    const fs::path lit = AuthorFloorWithLight(LightBakeContribution::Direct);
    const uint64_t litBase = CookDocument(lit, Root, 16.0, nullptr, nullptr, base).ContentHash;
    const uint64_t litReach = CookDocument(lit, Root, 16.0, nullptr, nullptr, widerReach).ContentHash;
    const uint64_t litOff = CookDocument(lit, Root, 16.0, nullptr, nullptr, disabled).ContentHash;
    EXPECT_NE(litBase, litReach);
    EXPECT_NE(litBase, litOff);

    const fs::path unlit = AuthorFloorWithLight(LightBakeContribution::None);
    const uint64_t unlitBase = CookDocument(unlit, Root, 16.0, nullptr, nullptr, base).ContentHash;
    const uint64_t unlitReach = CookDocument(unlit, Root, 16.0, nullptr, nullptr, widerReach).ContentHash;
    const uint64_t unlitOff = CookDocument(unlit, Root, 16.0, nullptr, nullptr, disabled).ContentHash;
    EXPECT_EQ(unlitBase, unlitReach);
    EXPECT_EQ(unlitBase, unlitOff);
}

TEST_F(BakedLightingCookTest, RestalesOnLightMove)
{
    const uint64_t before = CookDocument(
        AuthorFloorWithLight(LightBakeContribution::Direct, Vec3d{ 0, 4, 0 }),
        Root, 16.0).ContentHash;
    const uint64_t after = CookDocument(
        AuthorFloorWithLight(LightBakeContribution::Direct, Vec3d{ 1, 4, 0 }),
        Root, 16.0).ContentHash;
    EXPECT_NE(before, after);
}

TEST_F(BakedLightingCookTest, RestalesOnLightmapTuningOnlyWhenLightsExist)
{
    LightingCookParams fine{};
    LightingCookParams coarse{};
    coarse.LuxelSize = 0.5f;
    LightingCookParams wideCone{};
    wideCone.ConeDegrees = 80.0f;

    const fs::path lit = AuthorFloorWithLight(LightBakeContribution::Direct);
    const uint64_t litFine = CookDocument(lit, Root, 16.0, nullptr, nullptr, fine).ContentHash;
    const uint64_t litCoarse = CookDocument(lit, Root, 16.0, nullptr, nullptr, coarse).ContentHash;
    const uint64_t litCone = CookDocument(lit, Root, 16.0, nullptr, nullptr, wideCone).ContentHash;
    EXPECT_NE(litFine, litCoarse);
    EXPECT_NE(litFine, litCone);

    const fs::path unlit = AuthorFloorWithLight(LightBakeContribution::None);
    const uint64_t unlitFine = CookDocument(unlit, Root, 16.0, nullptr, nullptr, fine).ContentHash;
    const uint64_t unlitCoarse = CookDocument(unlit, Root, 16.0, nullptr, nullptr, coarse).ContentHash;
    EXPECT_EQ(unlitFine, unlitCoarse);
}

TEST_F(BakedLightingCookTest, RestalesOnCoplanarSoftEdgeToggle)
{
    // The two-quad sheet is geometrically identical either way; only the
    // chart grouping changes, so this passes only if chart identity is part
    // of the cook hash.
    const uint64_t hard = CookDocument(AuthorCoplanarPair(false), Root, 16.0).ContentHash;
    const uint64_t soft = CookDocument(AuthorCoplanarPair(true), Root, 16.0).ContentHash;
    EXPECT_NE(hard, soft);
}

TEST_F(BakedLightingCookTest, SplitCoplanarMeshesBakeWithoutChartOrDiagonalSeams)
{
    const DocumentCookResult original = CookDocument(
        AuthorSplitReceiver(false, "split_original"), Root, 16.0);
    const DocumentCookResult opposite = CookDocument(
        AuthorSplitReceiver(true, "split_opposite"), Root, 16.0);
    ASSERT_TRUE(original.Success) << original.Error;
    ASSERT_TRUE(opposite.Success) << opposite.Error;

    const fs::path originalRoot = Root / ".cooked/levels/split_original";
    const fs::path oppositeRoot = Root / ".cooked/levels/split_opposite";
    // Full-pipeline metamorphism: chart construction/packing, two-sided
    // shadow BVH, raster, dilation, and texel encoding all agree across the
    // diagonal.
    EXPECT_EQ(ReadBytes(originalRoot / "lightmap.stex"),
              ReadBytes(oppositeRoot / "lightmap.stex"));

    // The lighting is continuous across the duplicated boundary, so the two
    // one-sided charts must agree there. Each side's boundary luxels average
    // supersamples pulled up to a quarter luxel into their own chart, so the
    // agreement bound is that offset times the local radiance gradient, far
    // below the step a chart mismatch would produce.
    const std::array<Vec3d, 2> seam = SeamMidpointRadiance(originalRoot);
    EXPECT_LT((seam[0] - seam[1]).Magnitude(), 0.15f)
        << "duplicated coplanar edge sampled different chart radiance";
}

TEST_F(BakedLightingCookTest, WallOnMeshSeamKeepsShadowSideDark)
{
    // The lit floor, the wall base, and the mesh seam all coincide at x=3:
    // the radiance on the floor plane steps hard exactly along the seam. The
    // left chart must stay lit up to the wall and the right chart fully
    // shadowed, with no averaging across the seam; blending the two sides
    // into the shared boundary texels renders as a bright fringe hugging the
    // wall on its shadow side.
    const DocumentCookResult result = CookDocument(
        AuthorSeamWall("seam_wall"), Root, 16.0);
    ASSERT_TRUE(result.Success) << result.Error;

    const std::array<Vec3d, 2> seam =
        SeamMidpointRadiance(Root / ".cooked/levels/seam_wall");
    EXPECT_GT(seam[0].Magnitude(), 1.0f) << "lit side lost its one-sided limit";
    EXPECT_LT(seam[1].Magnitude(), 0.05f) << "light leaked across the seam";
}
