// A prefab is an ordinary scene document that happens to live outside
// levels/: the cook publishes it under its own directory
// (.cooked/prefabs/<stem>.smap), its baked artifacts land beside it, and the
// content-hash cache behaves exactly as it does for a level -- including a
// nested placed source defeating the hit.

#include "document/DocumentCook.h"
#include "document/DocumentRecook.h"
#include "document/EditorDocument.h"
#include "document/DocumentSerialization.h"
#include "CookedSmapReaders.h"

#include <assets/cook/CookedCache.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <render/PointLightComponent.h>
#include <world/scene/SmapFormat.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    class PrefabCookTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        void SetUp() override
        {
            Root = fs::temp_directory_path()
                / ("sencha_prefabcook_"
                   + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
            fs::remove_all(Root);
            fs::create_directories(Root);
            WriteFile("materials/dev/gray.smat", "{}");
        }
        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }

        void WriteFile(const std::string& rel, std::string_view contents)
        {
            const fs::path p = Root / rel;
            fs::create_directories(p.parent_path());
            std::ofstream(p, std::ios::trunc) << contents;
        }

        fs::path AuthorPrefab(const std::string& rel, Vec3d lightPosition)
        {
            EditorDocument doc(Logging);
            doc.SetContentRoots({ Root });
            const EntityId light = doc.GetScene().CreateEntity(lightPosition);
            PointLightComponent lamp{};
            lamp.Intensity = 7.0f;
            doc.GetScene().GetRegistry().Components.AddComponent(light, lamp);
            const fs::path path = Root / rel;
            fs::create_directories(path.parent_path());
            EXPECT_TRUE(doc.SaveAs(path.generic_string()));
            return path;
        }

        fs::path Root;
        LoggingProvider Logging;
    };
}

TEST_F(PrefabCookTest, APrefabCooksUnderItsOwnDirectory)
{
    const fs::path prefab = AuthorPrefab("prefabs/lamp.sscene", Vec3d{ 0, 2, 0 });

    const DocumentCookResult result = CookDocument(prefab, Root, 16.0);
    ASSERT_TRUE(result.Success) << result.Error;
    EXPECT_TRUE(fs::exists(Root / ".cooked/prefabs/lamp.smap"));
    EXPECT_FALSE(fs::exists(Root / ".cooked/levels/lamp.smap"));

    // The cooked scene still carries the runtime component.
    const SmapContents cooked = ReadCookedScene(Root / ".cooked/prefabs/lamp.smap");
    EXPECT_TRUE(FindFirstCookedComponent(cooked, "PointLight").has_value());
}

TEST_F(PrefabCookTest, SameStemLevelAndPrefabDoNotClobber)
{
    const fs::path prefab = AuthorPrefab("prefabs/foo.sscene", Vec3d{ 0, 2, 0 });
    const fs::path level = AuthorPrefab("levels/foo.sscene", Vec3d{ 5, 1, 0 });

    ASSERT_TRUE(CookDocument(prefab, Root, 16.0).Success);
    ASSERT_TRUE(CookDocument(level, Root, 16.0).Success);
    EXPECT_TRUE(fs::exists(Root / ".cooked/prefabs/foo.smap"));
    EXPECT_TRUE(fs::exists(Root / ".cooked/levels/foo.smap"));
}

TEST_F(PrefabCookTest, RecookHitsTheCacheUntilANestedSourceChanges)
{
    (void)AuthorPrefab("prefabs/bulb.sscene", Vec3d{ 0, 1, 0 });

    // A prefab that places another prefab: the flattened composition is what
    // cooks, so the placed source is part of the cook's identity.
    EditorDocument host(Logging);
    host.SetContentRoots({ Root });
    const SceneInstanceId placed = host.PlaceSceneInstance(
        "asset://prefabs/bulb.sscene", Transform3f::Identity(), {}, nullptr);
    ASSERT_TRUE(placed.IsValid());
    const fs::path hostPath = Root / "prefabs/fixture.sscene";
    fs::create_directories(hostPath.parent_path());
    ASSERT_TRUE(host.SaveAs(hostPath.generic_string()));

    ASSERT_TRUE(CookDocument(hostPath, Root, 16.0).Success);
    const DocumentCookResult hit = CookDocument(hostPath, Root, 16.0);
    ASSERT_TRUE(hit.Success);
    EXPECT_TRUE(hit.CacheHit);

    // Rewrite the nested source in place; the flattened scene changes, so the
    // hit must be defeated.
    std::string bulb;
    {
        std::ifstream in(Root / "prefabs/bulb.sscene");
        std::ostringstream buffer;
        buffer << in.rdbuf();
        bulb = buffer.str();
    }
    const std::size_t at = bulb.find("intensity: 7");
    ASSERT_NE(at, std::string::npos);
    bulb.replace(at, sizeof("intensity: 7") - 1, "intensity: 9");
    WriteFile("prefabs/bulb.sscene", bulb);

    const DocumentCookResult recook = CookDocument(hostPath, Root, 16.0);
    ASSERT_TRUE(recook.Success);
    EXPECT_FALSE(recook.CacheHit);
    const SmapContents cooked =
        ReadCookedScene(Root / ".cooked/prefabs/fixture.smap");
    const std::optional<JsonValue> lamp =
        FindFirstCookedComponent(cooked, "PointLight");
    ASSERT_TRUE(lamp.has_value());
    const JsonValue* intensity = lamp->Find("intensity");
    ASSERT_NE(intensity, nullptr);
    EXPECT_EQ(intensity->AsNumber(), 9.0);
}

namespace
{
    std::string ReadText(const fs::path& path)
    {
        std::ifstream in(path);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    std::optional<double> CookedIntensity(const fs::path& smap)
    {
        const std::optional<JsonValue> lamp = FindFirstCookedComponent(ReadCookedScene(smap), "PointLight");
        const JsonValue* intensity = lamp ? lamp->Find("intensity") : nullptr;
        return intensity != nullptr ? std::optional<double>(intensity->AsNumber()) : std::nullopt;
    }
}

// A prefab that extends another cooks flat: the runtime sees composed values,
// and the base it read is recorded so a later edit to it recooks the child.
TEST_F(PrefabCookTest, ADerivedPrefabCooksComposedAndRecooksWhenItsBaseIsSaved)
{
    (void)AuthorPrefab("prefabs/bulb.sscene", Vec3d{ 0, 1, 0 });
    const std::string base = ReadText(Root / "prefabs/bulb.sscene");
    WriteFile("prefabs/bright_bulb.sscene", "{ format_version: 2, extends: 'asset://prefabs/bulb.sscene', entities: [] }");

    const DocumentCookResult cooked = CookDocument(Root / "prefabs/bright_bulb.sscene", Root, 16.0);
    ASSERT_TRUE(cooked.Success) << cooked.Error;
    EXPECT_EQ(CookedIntensity(Root / ".cooked/prefabs/bright_bulb.smap"), 7.0);

    CookedCacheIndex index;
    ASSERT_TRUE(CookedCacheIndex::LoadFromFile((Root / ".cooked/index.json").generic_string(), index));
    const CookedSourceEntry* entry = index.Find("prefabs/bright_bulb.sscene");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->AdditionalSources.size(), 1u);
    EXPECT_EQ(entry->AdditionalSources[0].RelPath, "prefabs/bulb.sscene");

    std::string brighter = base;
    const std::size_t at = brighter.find("intensity: 7");
    ASSERT_NE(at, std::string::npos);
    brighter.replace(at, sizeof("intensity: 7") - 1, "intensity: 11");
    WriteFile("prefabs/bulb.sscene", brighter);
    const std::string saved[] = { std::string("asset://prefabs/bulb.sscene") };
    const std::vector<std::string> recooked = RecookComposedDependents(saved, Root, 16.0, Logging);
    EXPECT_EQ(recooked, std::vector<std::string>{ "prefabs/bright_bulb.sscene" });
    EXPECT_EQ(CookedIntensity(Root / ".cooked/prefabs/bright_bulb.smap"), 11.0);
}

TEST_F(PrefabCookTest, AnAbstractPrefabIsNotCooked)
{
    WriteFile("prefabs/fragments/glow.sscene",
              "{ format_version: 2, abstract: true, entities: [ { id: '00000000000000f1', components: {} } ] }");
    const DocumentCookResult refused = CookDocument(Root / "prefabs/fragments/glow.sscene", Root, 16.0);
    EXPECT_FALSE(refused.Success);
    EXPECT_NE(refused.Error.find("abstract"), std::string::npos) << refused.Error;
    EXPECT_FALSE(fs::exists(Root / ".cooked/prefabs/fragments/glow.smap"));
}

namespace
{
    constexpr std::string_view kHumanoid = R"({ format_version: 2, entities: [
        { id: '00000000000000a1', components: {
            Transform: { local: { position: [0, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] } },
            LookOrientation: { yaw: 0 },
            PointLight: { intensity: 5, range: 3 } } },
        { id: '00000000000000a2', parent: '00000000000000a1', components: {
            Transform: { local: { position: [0, 1, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] } } } } ] })";

    const JsonValue* RootComponent(const SmapContents& cooked, std::string_view name)
    {
        for (const SmapEntityRecord& record : cooked.Entities)
            if (record.Parent == SmapEntityRecord{}.Parent)
                return FindCookedComponent(record, name);
        return nullptr;
    }
}

// A guard extends a humanoid and includes two fragments, an elite guard extends
// the guard and patches one value; the elite guard cooks flat, as one spawnable
// root, and records every scene it composed from.
TEST_F(PrefabCookTest, AGuardChainCooksFlatAndRecordsItsSources)
{
    WriteFile("prefabs/actors/humanoid.sscene", kHumanoid);
    WriteFile("prefabs/fragments/alertable.sscene",
              "{ format_version: 2, abstract: true, entities: [ { id: '00000000000000f1', components: { AimFacing: {} } } ] }");
    WriteFile("prefabs/fragments/combat_training.sscene",
              "{ format_version: 2, abstract: true, entities: [ { id: '00000000000000f2', components: "
              "{ LookOrientation: { yaw: 1.5 } } } ] }");
    WriteFile("prefabs/actors/guard.sscene",
              "{ format_version: 2, extends: 'asset://prefabs/actors/humanoid.sscene', includes: [ "
              "{ fragment: 'asset://prefabs/fragments/alertable.sscene' }, "
              "{ fragment: 'asset://prefabs/fragments/combat_training.sscene' } ], entities: [] }");
    WriteFile("prefabs/actors/elite_guard.sscene",
              "{ format_version: 2, extends: 'asset://prefabs/actors/guard.sscene', entities: [], "
              "patch: { '00000000000000a1': { PointLight: { intensity: 9 } } } }");

    const DocumentCookResult cooked = CookDocument(Root / "prefabs/actors/elite_guard.sscene", Root, 16.0);
    ASSERT_TRUE(cooked.Success) << cooked.Error;
    const SmapContents scene = ReadCookedScene(Root / ".cooked/prefabs/actors/elite_guard.smap");
    EXPECT_EQ(CountRootRecords(scene), 1u) << "one root: spawnable as a prefab";
    EXPECT_EQ(scene.Entities.size(), 2u);
    ASSERT_NE(RootComponent(scene, "AimFacing"), nullptr) << "a fragment's component";
    const JsonValue* look = RootComponent(scene, "LookOrientation");
    ASSERT_NE(look, nullptr);
    EXPECT_FLOAT_EQ(static_cast<float>(look->Find("yaw")->AsNumber()), 1.5f) << "the other fragment's value";
    const JsonValue* light = RootComponent(scene, "PointLight");
    ASSERT_NE(light, nullptr);
    EXPECT_EQ(light->Find("intensity")->AsNumber(), 9.0) << "the elite guard's own patch";
    EXPECT_EQ(light->Find("range")->AsNumber(), 3.0) << "the humanoid's value, through the guard";
    EXPECT_EQ(RootComponent(scene, "scene_instance"), nullptr) << "inherited entities are the prefab's own";

    CookedCacheIndex index;
    ASSERT_TRUE(CookedCacheIndex::LoadFromFile((Root / ".cooked/index.json").generic_string(), index));
    const CookedSourceEntry* entry = index.Find("prefabs/actors/elite_guard.sscene");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->AdditionalSources.size(), 4u) << "the guard, the humanoid, and both fragments";
}
