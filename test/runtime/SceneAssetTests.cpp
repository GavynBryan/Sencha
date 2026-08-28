#include <assets/runtime/AssetPreloader.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/scene/SceneCache.h>
#include <core/logging/LoggingProvider.h>
#include <jobs/AsyncTaskQueue.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

//-----------------------------------------------------------------------------
// Test components. SceneAssetSkew reuses SceneAssetValue's stable name with a
// different field set -- the schema skew a resident load must refuse.
//-----------------------------------------------------------------------------

struct SceneAssetValue
{
    int Value = 0;
};

template <>
struct TypeSchema<SceneAssetValue>
{
    static constexpr std::string_view Name = "scene_asset_value";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('S', 'A', 'V', '1');

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &SceneAssetValue::Value),
        };
    }
};

struct SceneAssetSkew
{
    int Value = 0;
    float Extra = 0.0f;
};

template <>
struct TypeSchema<SceneAssetSkew>
{
    static constexpr std::string_view Name = "scene_asset_value";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('S', 'A', 'V', '1');

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &SceneAssetSkew::Value),
            MakeField("extra", &SceneAssetSkew::Extra),
        };
    }
};

namespace
{
    struct SceneAssetHarness
    {
        SceneAssetHarness()
            : Tasks(0)
            , Registry(Logging)
            , Scenes(Logging)
            , Assets(Logging, Registry, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, &Scenes, &Serializers)
            , Preloader(Logging, Registry, Assets, Tasks)
        {
            EXPECT_EQ(Serializers.Register(
                          std::make_unique<ComponentSerializer<SceneAssetValue>>()),
                      ComponentSerializerRegistry::RegisterResult::Added);
        }

        LoggingProvider Logging;
        AsyncTaskQueue Tasks;
        AssetRegistry Registry;
        ComponentSerializerRegistry Serializers;
        SceneCache Scenes;
        AssetSystem Assets;
        AssetPreloader Preloader;
    };

    // One temp .smap on disk plus its registry record, written by the given
    // serializer set so skew scenarios can cook against a different schema.
    class TempSceneAsset
    {
    public:
        TempSceneAsset(AssetRegistry& registry,
                       const ComponentSerializerRegistry& serializers,
                       std::string_view name)
        {
            SmapContents contents;
            contents.Dependencies.push_back(
                SmapDependency{ AssetId{ 9 }, "asset://meshes/prop.smesh" });
            SmapEntityRecord record;
            record.Components.emplace_back(
                MakeComponentTypeId("scene_asset_value"),
                JsonValue(JsonValue::Object{ { "value", JsonValue(7.0) } }));
            contents.Entities.push_back(std::move(record));

            std::vector<std::byte> bytes;
            SmapError error;
            EXPECT_TRUE(WriteSmap(contents, serializers, bytes, &error))
                << error.Message;

            static int counter = 0;
            File = std::filesystem::temp_directory_path()
                / ("sencha_scene_asset_" + std::string(name) + "_"
                   + std::to_string(++counter) + ".smap");
            std::ofstream out(File, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));

            Path = "asset://levels/" + std::string(name) + ".smap";
            EXPECT_TRUE(registry.Register(AssetRecord{
                .Type = AssetType::Scene,
                .SourceKind = AssetSourceKind::File,
                .Path = Path,
                .FilePath = File.generic_string(),
            }));
        }

        ~TempSceneAsset()
        {
            std::error_code ec;
            std::filesystem::remove(File, ec);
        }

        std::string Path;
        std::filesystem::path File;
    };
} // namespace

TEST(SceneAsset, LoadsThroughTheFrontDoorAndDedupsResidency)
{
    SceneAssetHarness h;
    TempSceneAsset scene(h.Registry, h.Serializers, "hall");

    const SceneHandle first = h.Assets.LoadScene(scene.Path);
    ASSERT_TRUE(first.IsValid());

    const SmapContents* contents = h.Scenes.Get(first);
    ASSERT_NE(contents, nullptr);
    ASSERT_EQ(contents->Entities.size(), 1u);
    ASSERT_EQ(contents->Dependencies.size(), 1u);
    EXPECT_EQ(contents->Dependencies[0].Path, "asset://meshes/prop.smesh");

    // A second load acquires the resident entry instead of re-parsing.
    const SceneHandle second = h.Assets.LoadScene(scene.Path);
    EXPECT_EQ(second, first);

    h.Assets.ReleaseScene(second);
    EXPECT_TRUE(h.Scenes.Find(scene.Path).IsValid());
    h.Assets.ReleaseScene(first);
    EXPECT_FALSE(h.Scenes.Find(scene.Path).IsValid());
}

TEST(SceneAsset, SchemaSkewRefusesTheLoad)
{
    SceneAssetHarness h;

    // Cooked by a build whose scene_asset_value carried an extra field.
    ComponentSerializerRegistry skewed;
    ASSERT_EQ(skewed.Register(std::make_unique<ComponentSerializer<SceneAssetSkew>>()),
              ComponentSerializerRegistry::RegisterResult::Added);
    TempSceneAsset scene(h.Registry, skewed, "skewed");

    EXPECT_FALSE(h.Assets.LoadScene(scene.Path).IsValid());
    EXPECT_FALSE(h.Scenes.Find(scene.Path).IsValid());
}

TEST(SceneAsset, MissingFileFailsWithoutResidency)
{
    SceneAssetHarness h;
    EXPECT_TRUE(h.Registry.Register(AssetRecord{
        .Type = AssetType::Scene,
        .SourceKind = AssetSourceKind::File,
        .Path = "asset://levels/gone.smap",
        .FilePath = "does/not/exist.smap",
    }));

    EXPECT_FALSE(h.Assets.LoadScene("asset://levels/gone.smap").IsValid());
    EXPECT_FALSE(h.Scenes.Find("asset://levels/gone.smap").IsValid());
}

TEST(SceneAsset, PreloadStreamsSceneToResidency)
{
    SceneAssetHarness h;
    TempSceneAsset scene(h.Registry, h.Serializers, "streamed");

    const std::vector<std::string> paths{ scene.Path };
    auto preload = h.Preloader.Begin(paths);
    EXPECT_FALSE(preload->IsComplete());
    EXPECT_FALSE(h.Scenes.Find(scene.Path).IsValid());

    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);

    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 0u);
    EXPECT_TRUE(h.Scenes.Find(scene.Path).IsValid());

    // The preload's handle is the only reference; releasing it frees the entry.
    preload->ReleaseAll();
    EXPECT_FALSE(h.Scenes.Find(scene.Path).IsValid());
}
