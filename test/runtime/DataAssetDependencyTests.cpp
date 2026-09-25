// A resident data asset holds the dependencies its compile declared, whichever
// load path committed it, so a consumer binding the value later never finds a
// dependency missing or freed.

#include <gtest/gtest.h>

#include <anim/AnimFactSchema.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    struct SchemaChain
    {
        std::filesystem::path Root =
            std::filesystem::temp_directory_path() / "sencha_data_dependency_tests";
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };

        SchemaChain()
        {
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root / "animation");
            Write("base.facts.sdata", R"({ "type": "animation.fact_schema", "version": 1,
                "data": { "slots": [ { "name": "Grounded", "kind": "bool" } ] } })");
            Write("game.facts.sdata", R"({ "type": "animation.fact_schema", "version": 1,
                "data": { "extends": "asset://animation/base.facts.sdata",
                          "slots": [ { "name": "Crouched", "kind": "bool" } ] } })");
            (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        }

        ~SchemaChain() { std::filesystem::remove_all(Root); }

        void Write(const std::string& name, const std::string& text)
        {
            std::ofstream(Root / "animation" / name) << text;
        }

        [[nodiscard]] bool Resident(std::string_view path) const
        {
            return Assets.DataAssets.Find(path).IsValid();
        }
    };
}

TEST(DataAssetDependencies, ALoadedValueHoldsWhatItDeclared)
{
    SchemaChain chain;
    AssetLease game = chain.Assets.Assets.LoadLease("asset://animation/game.facts.sdata", AssetType::Data);
    ASSERT_TRUE(game);
    EXPECT_TRUE(chain.Resident("asset://animation/base.facts.sdata"));

    game.Reset();
    EXPECT_FALSE(chain.Resident("asset://animation/game.facts.sdata"));
    EXPECT_FALSE(chain.Resident("asset://animation/base.facts.sdata"));
}

TEST(DataAssetDependencies, AMissingDependencyFailsTheLoad)
{
    SchemaChain chain;
    chain.Write("orphan.facts.sdata", R"({ "type": "animation.fact_schema", "version": 1,
        "data": { "extends": "asset://animation/absent.facts.sdata", "slots": [] } })");
    (void)ScanAssetsDirectory(chain.Root.generic_string(), chain.Assets.Registry,
                              chain.Assets.Assets.Kinds());
    EXPECT_FALSE(chain.Assets.Assets.LoadLease("asset://animation/orphan.facts.sdata", AssetType::Data));
}

TEST(DataAssetDependencies, AReloadSwapsTheHeldSet)
{
    SchemaChain chain;
    chain.Write("other.facts.sdata", R"({ "type": "animation.fact_schema", "version": 1,
        "data": { "slots": [ { "name": "Wet", "kind": "bool" } ] } })");
    (void)ScanAssetsDirectory(chain.Root.generic_string(), chain.Assets.Registry,
                              chain.Assets.Assets.Kinds());
    AssetLease game = chain.Assets.Assets.LoadLease("asset://animation/game.facts.sdata", AssetType::Data);
    ASSERT_TRUE(game);

    chain.Write("game.facts.sdata", R"({ "type": "animation.fact_schema", "version": 1,
        "data": { "extends": "asset://animation/other.facts.sdata", "slots": [] } })");
    const AssetRecord* record = chain.Assets.Registry.FindByPath("asset://animation/game.facts.sdata");
    ASSERT_NE(record, nullptr);
    AssetSystem& assets = chain.Assets.Assets;
    ASSERT_TRUE(assets.Reload(assets.LoaderFor(AssetType::Data)->LoadStaged(*record, assets.DefaultSource())));

    EXPECT_TRUE(chain.Resident("asset://animation/other.facts.sdata"));
    EXPECT_FALSE(chain.Resident("asset://animation/base.facts.sdata"));
}

TEST(DataAssetDependencies, ACycleFailsInsteadOfRecursing)
{
    SchemaChain chain;
    chain.Write("left.facts.sdata", R"({ "type": "animation.fact_schema", "version": 1,
        "data": { "extends": "asset://animation/right.facts.sdata", "slots": [] } })");
    chain.Write("right.facts.sdata", R"({ "type": "animation.fact_schema", "version": 1,
        "data": { "extends": "asset://animation/left.facts.sdata", "slots": [] } })");
    (void)ScanAssetsDirectory(chain.Root.generic_string(), chain.Assets.Registry,
                              chain.Assets.Assets.Kinds());
    EXPECT_FALSE(chain.Assets.Assets.LoadLease("asset://animation/left.facts.sdata", AssetType::Data));
    EXPECT_FALSE(chain.Resident("asset://animation/right.facts.sdata"));
}
