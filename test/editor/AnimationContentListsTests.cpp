// The animation panels list the project's assets by kind and data assets by
// their declared subtype, whatever subtypes the project has.

#include "authoring/AnimationContentLists.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

TEST(AnimationContentLists, ListsDataAssetsBySubtypeAndOthersByKind)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / ("sencha_content_lists_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    std::ofstream(root / "b.sdata") << R"({ "type": "animation.fact_schema", "version": 1, "data": { "slots": [] } })";
    std::ofstream(root / "a.sdata") << R"({ "type": "animation.fact_schema", "version": 1, "data": { "slots": [] } })";
    std::ofstream(root / "custom.sdata") << R"({ "type": "game.custom", "version": 1, "data": {} })";

    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets(logging, serializers);
    (void)ScanAssetsDirectory(root.generic_string(), assets.Registry, assets.Assets.Kinds());
    ASSERT_TRUE(assets.Registry.RegisterOrVerify(AssetRecord{ .Type = AssetType::AnimationClip,
                                                              .SourceKind = AssetSourceKind::Procedural,
                                                              .Path = "asset://walk.sanim" }));

    AnimationContentLists lists;
    lists.Refresh(assets.Registry, assets.Assets.DefaultSource());
    const auto facts = lists.OfSubtype("animation.fact_schema");
    ASSERT_EQ(facts.size(), 2u);
    EXPECT_EQ(facts[0], "asset://a.sdata") << "sorted";
    EXPECT_EQ(lists.OfSubtype("game.custom").size(), 1u) << "a subtype nothing names in code still lists";
    EXPECT_TRUE(lists.OfSubtype("animation.rig").empty());
    ASSERT_EQ(lists.Of(AssetType::AnimationClip).size(), 1u);
    EXPECT_EQ(lists.Of(AssetType::Data).size(), 3u);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
