// Evidence generator: cooks a matrix of scenes that vary shadow caster count
// and shadow-casting light count independently, so per-scope CPU cost can be
// read against each axis separately.
//
//   levels/scale_c<casters>_s<shadowLights>
//
// Shadow command generation runs one visibility gather per view, and a point
// light is six views, so caster cost and view cost are separable only if the
// scenes hold one axis still while moving the other. Nothing here is baked:
// no Direct lights and no probe volume, so cook time stays proportional to
// geometry and the runtime cost under test is the dynamic path.
//
// Skipped unless SENCHA_SCALE_BENCH_ROOT names the assets root to cook into.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

#include <core/assets/AssetRef.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
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

// Casters on a square grid inside the shell, all one mesh shape and one
// material so draw batching is not the variable under test.
void AuthorCasterField(EditorDocument& doc, int count, float halfExtent)
{
    if (count <= 0)
        return;
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count))));
    const float spacing = (halfExtent * 2.0f) / static_cast<float>(side + 1);
    int placed = 0;
    for (int gz = 0; gz < side && placed < count; ++gz)
    {
        for (int gx = 0; gx < side && placed < count; ++gx)
        {
            const float x = -halfExtent + spacing * static_cast<float>(gx + 1);
            const float z = -halfExtent + spacing * static_cast<float>(gz + 1);
            doc.GetScene().CreateBrush(Vec3d{ x, 0.6f, z }, Vec3d{ 0.2f, 0.6f, 0.2f });
            ++placed;
        }
    }
}

// Shadow-casting spots ringing the field and aimed down, so every caster sits
// inside some view's frustum rather than being culled before the cost appears.
void AuthorShadowLights(EditorDocument& doc, int count, float ringRadius,
                        float height, float range)
{
    for (int i = 0; i < count; ++i)
    {
        const float angle =
            kHalfPi * 4.0f * static_cast<float>(i) / static_cast<float>(count);
        const Vec3d position{ ringRadius * std::cos(angle), height,
                              ringRadius * std::sin(angle) };
        const EntityId entity = doc.GetScene().CreateEntity(position);
        if (LocalTransform* transform =
                doc.GetScene().GetRegistry().Components.TryGet<LocalTransform>(entity))
            transform->Value.Rotation = Quatf::FromAxisAngle(Vec3d::Right(), -kHalfPi);
        SpotLightComponent light{};
        light.Color = Vec3d{ 1.0f, 1.0f, 1.0f };
        light.Intensity = 20.0f;
        light.Range = range;
        light.InnerAngleDegrees = 45.0f;
        light.OuterAngleDegrees = 70.0f;
        light.CastShadows = true;
        doc.GetScene().GetRegistry().Components.AddComponent(entity, light);
    }
}

void CookAuthoredLevel(EditorDocument& doc, const fs::path& root,
                       const std::string& stem)
{
    const fs::path levelPath = root / "levels" / (stem + ".sscene");
    fs::create_directories(levelPath.parent_path());
    ASSERT_TRUE(doc.SaveAs(levelPath.generic_string()));
    const DocumentCookResult result = CookDocument(levelPath, root, /*cellSize*/ 16.0);
    ASSERT_TRUE(result.Success) << result.Error;
}

void GenerateScene(const fs::path& root, const AssetRef& material, int casters,
                   int shadowLights, LoggingProvider& logging)
{
    EditorDocument doc(logging);
    doc.SetDefaultMaterial(material);
    AuthorRoomShell(doc, 20.0f, 20.0f, 8.0f);
    AuthorCasterField(doc, casters, 16.0f);
    AuthorShadowLights(doc, shadowLights, 12.0f, 7.0f, 30.0f);
    CookAuthoredLevel(doc, root,
                      "scale_c" + std::to_string(casters) + "_s"
                          + std::to_string(shadowLights));
}
} // namespace

TEST(RenderScaleBench, Generate)
{
    const char* rootEnv = std::getenv("SENCHA_SCALE_BENCH_ROOT");
    if (rootEnv == nullptr)
        GTEST_SKIP() << "set SENCHA_SCALE_BENCH_ROOT to cook the scale bench scenes";
    const fs::path root(rootEnv);

    RegisterDocumentSerializers();
    LoggingProvider logging;

    const fs::path materialPath = root / "materials/dev/gray.smat";
    fs::create_directories(materialPath.parent_path());
    if (!fs::exists(materialPath))
        std::ofstream(materialPath, std::ios::trunc) << "{}";
    const AssetRef gray{ AssetType::Material, "asset://materials/dev/gray.smat" };

    // Caster axis at one shadow view, then light axis at the middle caster
    // count. The shared (400, 1) corner is what makes the two readable
    // against each other.
    GenerateScene(root, gray, 100, 1, logging);
    GenerateScene(root, gray, 400, 1, logging);
    GenerateScene(root, gray, 1600, 1, logging);
    GenerateScene(root, gray, 400, 4, logging);
    GenerateScene(root, gray, 400, 8, logging);
}
