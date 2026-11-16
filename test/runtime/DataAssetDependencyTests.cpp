// A resident data asset holds the dependencies its compile declared, whichever
// load path committed it, so a consumer binding the value later never finds a
// dependency missing or freed.

#include <gtest/gtest.h>

#include <anim/AnimFactSchema.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    std::string Facts(std::string_view slot)
    {
        return std::string(R"({ "type": "animation.fact_schema", "version": 2, "data": { "slots": [ { "name": ")")
            + std::string(slot) + R"(", "kind": "bool" } ] } })";
    }

    std::string Rig(std::string_view facts)
    {
        return std::string(R"({ "type": "animation.rig", "version": 1, "data": { "facts": ")") + std::string(facts)
            + R"(", "layers": [ { "name": "anim.layer.base" } ] } })";
    }

    struct DependencyChain
    {
        std::filesystem::path Root =
            std::filesystem::temp_directory_path() / "sencha_data_dependency_tests";
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };

        DependencyChain()
        {
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root / "animation");
            Write("base.facts.sdata", Facts("Grounded"));
            Write("game.rig.sdata", Rig("asset://animation/base.facts.sdata"));
            (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        }

        ~DependencyChain() { std::filesystem::remove_all(Root); }

        void Write(const std::string& name, const std::string& text)
        {
            std::ofstream(Root / "animation" / name, std::ios::trunc) << text;
        }

        void Rescan() { (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds()); }

        [[nodiscard]] bool Resident(std::string_view path) const
        {
            return Assets.DataAssets.Find(path).IsValid();
        }
    };
}

TEST(DataAssetDependencies, ALoadedValueHoldsWhatItDeclared)
{
    DependencyChain chain;
    AssetLease rig = chain.Assets.Assets.LoadLease("asset://animation/game.rig.sdata", AssetType::Data);
    ASSERT_TRUE(rig);
    EXPECT_TRUE(chain.Resident("asset://animation/base.facts.sdata"));

    rig.Reset();
    EXPECT_FALSE(chain.Resident("asset://animation/game.rig.sdata"));
    EXPECT_FALSE(chain.Resident("asset://animation/base.facts.sdata"));
}

TEST(DataAssetDependencies, AMissingDependencyFailsTheLoad)
{
    DependencyChain chain;
    chain.Write("orphan.rig.sdata", Rig("asset://animation/absent.facts.sdata"));
    chain.Rescan();
    EXPECT_FALSE(chain.Assets.Assets.LoadLease("asset://animation/orphan.rig.sdata", AssetType::Data));
}

TEST(DataAssetDependencies, AReloadSwapsTheHeldSet)
{
    DependencyChain chain;
    chain.Write("other.facts.sdata", Facts("Wet"));
    chain.Rescan();
    AssetLease rig = chain.Assets.Assets.LoadLease("asset://animation/game.rig.sdata", AssetType::Data);
    ASSERT_TRUE(rig);

    chain.Write("game.rig.sdata", Rig("asset://animation/other.facts.sdata"));
    const AssetRecord* record = chain.Assets.Registry.FindByPath("asset://animation/game.rig.sdata");
    ASSERT_NE(record, nullptr);
    AssetSystem& assets = chain.Assets.Assets;
    ASSERT_TRUE(assets.Reload(assets.LoaderFor(AssetType::Data)->LoadStaged(*record, assets.DefaultSource(), AssetPathsById{})));

    EXPECT_TRUE(chain.Resident("asset://animation/other.facts.sdata"));
    EXPECT_FALSE(chain.Resident("asset://animation/base.facts.sdata"));
}

// A working copy staged from memory reloads exactly as the file would, dependencies included.
TEST(DataAssetDependencies, AnInMemoryRootReloadsLikeItsFile)
{
    DependencyChain chain;
    chain.Write("other.facts.sdata", Facts("Wet"));
    chain.Rescan();
    AssetLease rig = chain.Assets.Assets.LoadLease("asset://animation/game.rig.sdata", AssetType::Data);
    ASSERT_TRUE(rig);

    const JsonValue working = *JsonParse(Rig("asset://animation/other.facts.sdata"));
    const AssetRecord* record = chain.Assets.Registry.FindByPath("asset://animation/game.rig.sdata");
    ASSERT_NE(record, nullptr);
    ASSERT_TRUE(chain.Assets.Assets.Reload(chain.Assets.StageDataRoot(*record, working)));
    EXPECT_TRUE(chain.Resident("asset://animation/other.facts.sdata"));
    EXPECT_FALSE(chain.Resident("asset://animation/base.facts.sdata"));

    const AssetStaging refused = chain.Assets.StageDataRoot(*record, *JsonParse(R"({ "type": "nope" })"));
    EXPECT_FALSE(refused.Error.empty());
}

TEST(DataAssetDependencies, ACycleFailsInsteadOfRecursing)
{
    DependencyChain chain;
    chain.Write("left.rig.sdata", Rig("asset://animation/right.rig.sdata"));
    chain.Write("right.rig.sdata", Rig("asset://animation/left.rig.sdata"));
    chain.Rescan();
    EXPECT_FALSE(chain.Assets.Assets.LoadLease("asset://animation/left.rig.sdata", AssetType::Data));
    EXPECT_FALSE(chain.Resident("asset://animation/right.rig.sdata"));
}
