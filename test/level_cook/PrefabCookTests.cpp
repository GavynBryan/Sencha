// A prefab is an ordinary scene document that happens to live outside
// levels/: the cook publishes it under its own directory
// (.cooked/prefabs/<stem>.smap), its baked artifacts land beside it, and the
// content-hash cache behaves exactly as it does for a level -- including a
// nested placed source defeating the hit.

#include "document/DocumentCook.h"
#include "document/EditorDocument.h"
#include "document/DocumentSerialization.h"
#include "CookedSmapReaders.h"

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
