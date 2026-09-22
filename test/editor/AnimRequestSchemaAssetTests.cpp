#include <anim/AnimRequestSchema.h>
#include <assets/runtime/RuntimeAssets.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>

TEST(AnimRequestSchemaAsset, HeadlessRuntimeLoadsThroughRegisteredDataAssetPipeline)
{
    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets(logging, serializers);
    constexpr const char* path = "asset://animation/requests.sdata";
    AssetRecord record;
    record.Type = AssetType::Data;
    record.SourceKind = AssetSourceKind::File;
    record.Path = path;
    record.FilePath = (std::filesystem::path(SENCHA_REPO_ROOT)
        / "test/fixtures/content/assets/animation/requests.sdata").generic_string();
    ASSERT_TRUE(assets.Registry.Register(record));
    auto lease = assets.Assets.LoadLease(path, AssetType::Data);
    ASSERT_TRUE(lease);
    const auto handle = DataAssetHandle::FromToken(lease.OpaqueToken());
    const auto* schema = assets.DataAssets.TryGet<AnimRequestSchema>(handle, kAnimRequestSchemaType);
    ASSERT_NE(schema, nullptr);
    ASSERT_EQ(schema->Intents.size(), 3u);
    EXPECT_EQ(schema->Intents[0].Intent, "Anim.Reload");
    EXPECT_EQ(schema->Intents[1].Params[0].Kind, AnimRequestParamKind::Int);
    EXPECT_FALSE(assets.DataTypes.Unregister(kAnimRequestSchemaType));
    lease.Reset();
    EXPECT_EQ(assets.DataAssets.TryGet<AnimRequestSchema>(handle, kAnimRequestSchemaType), nullptr);
}
