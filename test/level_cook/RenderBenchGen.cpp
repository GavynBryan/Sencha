// Evidence generator: cooks the two renderer-review benchmark scenes
// (renderer plan Section 11, Phase 3C) for live GPU capture runs:
//
//   levels/bench_hub     representative content: an enclosed hub sized to
//                        contain the scripted camera orbit, baked direct
//                        lights + probe volume + AO, and 12 dynamic lights
//                        of which 6 cast shadows (4 spot + 2 point).
//   levels/bench_stress  worst case: 64 visible dynamic lights with heavy
//                        pool overlap (52 plain + 8 shadow spots + 4 shadow
//                        points), pillars as casters, probes resident.
//
// Skipped unless SENCHA_RENDER_BENCH_ROOT names the assets root to cook into.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

#include <core/assets/AssetRef.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/LightComponentTypes.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
namespace fs = std::filesystem;
constexpr float kHalfPi = 1.57079632679f;

// Floor, ceiling, and four perimeter walls of an enclosed room centered on
// the origin: interior half extents (halfX, wallHeight/2, halfZ).
void AuthorRoomShell(EditorDocument& doc, float halfX, float halfZ, float height)
{
    EditorScene& scene = doc.GetScene();
    const float halfH = height * 0.5f;
    scene.CreateBrush(Vec3d{ 0, -0.25f, 0 }, Vec3d{ halfX + 0.5f, 0.25f, halfZ + 0.5f });
    scene.CreateBrush(Vec3d{ 0, height + 0.25f, 0 },
                      Vec3d{ halfX + 0.5f, 0.25f, halfZ + 0.5f });
    scene.CreateBrush(Vec3d{ -halfX - 0.25f, halfH, 0 }, Vec3d{ 0.25f, halfH, halfZ + 0.5f });
    scene.CreateBrush(Vec3d{ halfX + 0.25f, halfH, 0 }, Vec3d{ 0.25f, halfH, halfZ + 0.5f });
    scene.CreateBrush(Vec3d{ 0, halfH, -halfZ - 0.25f }, Vec3d{ halfX, halfH, 0.25f });
    scene.CreateBrush(Vec3d{ 0, halfH, halfZ + 0.25f }, Vec3d{ halfX, halfH, 0.25f });
}

EntityId AddPointLight(EditorDocument& doc, Vec3d position, Vec3d color,
                       float intensity, float range, bool castShadows,
                       LightBakeContribution contribution)
{
    const EntityId entity = doc.GetScene().CreateEntity(position);
    PointLightComponent light{};
    light.Color = color;
    light.Intensity = intensity;
    light.Range = range;
    light.CastShadows = castShadows;
    light.BakeContribution = contribution;
    doc.GetScene().GetRegistry().Components.AddComponent(entity, light);
    return entity;
}

// A downward-aimed spot (local Forward is -Z; -90 degrees about Right maps
// it to -Y).
EntityId AddDownSpot(EditorDocument& doc, Vec3d position, Vec3d color,
                     float intensity, float range, bool castShadows)
{
    const EntityId entity = doc.GetScene().CreateEntity(position);
    if (LocalTransform* transform =
            doc.GetScene().GetRegistry().Components.TryGet<LocalTransform>(entity))
        transform->Value.Rotation = Quatf::FromAxisAngle(Vec3d::Right(), -kHalfPi);
    SpotLightComponent light{};
    light.Color = color;
    light.Intensity = intensity;
    light.Range = range;
    light.InnerAngleDegrees = 25.0f;
    light.OuterAngleDegrees = 40.0f;
    light.CastShadows = castShadows;
    doc.GetScene().GetRegistry().Components.AddComponent(entity, light);
    return entity;
}

void AddProbeVolume(EditorDocument& doc, Vec3d center, Vec3d halfExtents,
                    float cellSize)
{
    const EntityId entity = doc.GetScene().CreateEntity(center);
    IrradianceVolumeComponent volume{};
    volume.HalfExtents = halfExtents;
    volume.CellSize = cellSize;
    doc.GetScene().GetRegistry().Components.AddComponent(entity, volume);
}

void CookAuthoredLevel(EditorDocument& doc, const fs::path& root,
                       const char* stem)
{
    const fs::path levelPath = root / "levels" / (std::string(stem) + ".sscene");
    fs::create_directories(levelPath.parent_path());
    ASSERT_TRUE(doc.SaveAs(levelPath.generic_string()));
    const DocumentCookResult result = CookDocument(levelPath, root, /*cellSize*/ 16.0);
    ASSERT_TRUE(result.Success) << result.Error;
}
} // namespace

TEST(RenderBench, Generate)
{
    const char* rootEnv = std::getenv("SENCHA_RENDER_BENCH_ROOT");
    if (rootEnv == nullptr)
        GTEST_SKIP() << "set SENCHA_RENDER_BENCH_ROOT to cook the bench scenes";
    const fs::path root(rootEnv);

    RegisterDocumentSerializers();
    LoggingProvider logging;

    const fs::path materialPath = root / "materials/dev/gray.smat";
    fs::create_directories(materialPath.parent_path());
    if (!fs::exists(materialPath))
        std::ofstream(materialPath, std::ios::trunc) << "{}";
    const AssetRef gray{ AssetType::Material, "asset://materials/dev/gray.smat" };

    // Hub: the camera orbit (radius 8, height 3) stays inside the shell and
    // clear of the pillar ring.
    {
        EditorDocument doc(logging);
        doc.SetDefaultMaterial(gray);
        AuthorRoomShell(doc, 15.0f, 15.0f, 6.0f);
        for (const float x : { -4.0f, 4.0f })
            for (const float z : { -4.0f, 4.0f })
                doc.GetScene().CreateBrush(Vec3d{ x, 3.0f, z },
                                           Vec3d{ 0.5f, 3.0f, 0.5f });

        // Baked layer: four warm Direct corner lights feed the lightmap, the
        // AO plane, and the probe bounce; zero runtime cost.
        for (const float x : { -10.0f, 10.0f })
            for (const float z : { -10.0f, 10.0f })
                AddPointLight(doc, Vec3d{ x, 4.5f, z }, Vec3d{ 1.0f, 0.85f, 0.6f },
                              20.0f, 16.0f, false, LightBakeContribution::Direct);
        AddProbeVolume(doc, Vec3d{ 0, 3, 0 }, Vec3d(15.0f, 3.0f, 15.0f), 1.5f);

        // Dynamic layer: 12 lights, 6 shadow-casting (4 spot + 2 point).
        int spotIndex = 0;
        for (const float x : { -6.0f, 6.0f })
            for (const float z : { -6.0f, 6.0f })
            {
                const float hue = static_cast<float>(spotIndex++) / 4.0f;
                AddDownSpot(doc, Vec3d{ x, 5.0f, z },
                            Vec3d{ 0.7f + 0.3f * hue, 0.8f, 1.0f - 0.3f * hue },
                            18.0f, 9.0f, true);
            }
        AddPointLight(doc, Vec3d{ 0, 2.5f, -8 }, Vec3d{ 1.0f, 0.6f, 0.4f },
                      12.0f, 8.0f, true, LightBakeContribution::None);
        AddPointLight(doc, Vec3d{ 0, 2.5f, 8 }, Vec3d{ 0.4f, 0.7f, 1.0f },
                      12.0f, 8.0f, true, LightBakeContribution::None);
        for (int i = 0; i < 6; ++i)
        {
            const float angle = kHalfPi * 4.0f * static_cast<float>(i) / 6.0f;
            AddPointLight(doc,
                          Vec3d{ 11.0f * std::cos(angle), 1.5f,
                                 11.0f * std::sin(angle) },
                          Vec3d{ 0.9f, 0.9f, 0.8f }, 6.0f, 5.0f, false,
                          LightBakeContribution::None);
        }
        CookAuthoredLevel(doc, root, "bench_hub");
    }

    // Stress: exactly 64 visible dynamic lights (the forward cap) with
    // overlapping pools, all within the orbit's inward view.
    {
        EditorDocument doc(logging);
        doc.SetDefaultMaterial(gray);
        AuthorRoomShell(doc, 18.0f, 18.0f, 8.0f);
        for (const float x : { -3.0f, 3.0f })
            for (const float z : { -3.0f, 3.0f })
                doc.GetScene().CreateBrush(Vec3d{ x, 2.0f, z },
                                           Vec3d{ 0.4f, 2.0f, 0.4f });

        for (const float x : { -12.0f, 12.0f })
            AddPointLight(doc, Vec3d{ x, 6.0f, 0 }, Vec3d{ 1.0f, 0.9f, 0.7f },
                          25.0f, 20.0f, false, LightBakeContribution::Direct);
        AddProbeVolume(doc, Vec3d{ 0, 4, 0 }, Vec3d(18.0f, 4.0f, 18.0f), 2.0f);

        // 52 plain points on an 8x8 grid minus the last 12 slots, spacing
        // 1.4 keeps the whole field inside the orbit radius.
        int placed = 0;
        for (int gy = 0; gy < 8 && placed < 52; ++gy)
            for (int gx = 0; gx < 8 && placed < 52; ++gx)
            {
                const float x = (static_cast<float>(gx) - 3.5f) * 1.4f;
                const float z = (static_cast<float>(gy) - 3.5f) * 1.4f;
                AddPointLight(doc, Vec3d{ x, 1.0f, z },
                              Vec3d{ 0.6f + 0.4f * (placed % 3 == 0),
                                     0.6f + 0.4f * (placed % 3 == 1),
                                     0.6f + 0.4f * (placed % 3 == 2) },
                              4.0f, 3.0f, false, LightBakeContribution::None);
                ++placed;
            }
        // 8 shadow spots ring the field from above; 4 shadow points inside it.
        for (int i = 0; i < 8; ++i)
        {
            const float angle = kHalfPi * 4.0f * static_cast<float>(i) / 8.0f;
            AddDownSpot(doc,
                        Vec3d{ 5.5f * std::cos(angle), 7.0f, 5.5f * std::sin(angle) },
                        Vec3d{ 0.9f, 0.9f, 1.0f }, 20.0f, 10.0f, true);
        }
        for (const float x : { -2.0f, 2.0f })
            for (const float z : { -2.0f, 2.0f })
                AddPointLight(doc, Vec3d{ x, 3.0f, z }, Vec3d{ 1.0f, 0.8f, 0.6f },
                              10.0f, 7.0f, true, LightBakeContribution::None);
        CookAuthoredLevel(doc, root, "bench_stress");
    }
}
