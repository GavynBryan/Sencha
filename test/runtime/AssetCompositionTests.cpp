// RuntimeAssets is the engine's asset composition: every cache, the loader for
// each, and the front door they register with. Two properties of that
// composition are checked here -- that every built-in kind arrives at the front
// door, and that the caches are torn down in an order where a reference is
// always released into something that still exists.
//
// What a given process can *hold* is a separate question, answered by
// HasStore and covered by HeadlessAssetCapabilityTests.

#include <gtest/gtest.h>

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetKindRegistry.h>
#include <core/assets/AssetLease.h>
#include <core/logging/LoggingProvider.h>
#include <render/Material.h>
#include <render/MaterialCache.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

namespace
{
    // The headless composition is the one a test can build: the windowed
    // constructor takes Vulkan services. Everything asserted below is decided
    // by the shared constructor body, which both compositions run.
    class AssetComposition : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            RegisterEngineSceneSerializers(Serializers);
            Assets = std::make_unique<RuntimeAssets>(Logging, Serializers);
        }

        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        std::unique_ptr<RuntimeAssets> Assets;
    };
}

// Adding a kind means registering it here; a kind the front door never heard of
// is invisible to the scanner, the preloader and the hot reloader alike.
TEST_F(AssetComposition, EveryBuiltinKindReachesTheFrontDoor)
{
    for (const AssetType type : BuiltinAssetKinds())
    {
        const AssetKindRegistration* kind = Assets->Assets.Kinds().Find(type);
        ASSERT_NE(kind, nullptr) << "built-in kind " << static_cast<int>(type)
                                 << " is not registered";
        EXPECT_FALSE(kind->Name.empty());
        EXPECT_FALSE(kind->RuntimeExtensions.empty());

        // Staging touches no cache, so it is wired for every kind -- including
        // the ones this process has no cache to commit into.
        EXPECT_NE(kind->Stager, nullptr) << kind->Name << " cannot be staged";
    }
}

// A kind with a cache carries the whole load half; one without carries none of
// it, which is what makes "this process cannot hold a mesh" a fact about the
// composition rather than a load failure.
TEST_F(AssetComposition, ACacheBringsTheCommitHalfWithIt)
{
    for (const AssetKindRegistration& kind : Assets->Assets.Kinds().Entries())
    {
        EXPECT_EQ(kind.Store != nullptr, kind.Commit != nullptr) << kind.Name;
        EXPECT_EQ(kind.Store != nullptr, kind.IsLoadable()) << kind.Name;
        if (kind.Store == nullptr)
            continue;
        EXPECT_EQ(kind.Store->Type(), kind.Type) << kind.Name;
    }
}

// Materials are the one kind a field names several of at once, so they are the
// one kind with a list store behind them.
TEST_F(AssetComposition, MaterialsAreTheKindWithAListForm)
{
    for (const AssetKindRegistration& kind : Assets->Assets.Kinds().Entries())
    {
        const IAssetListStore* expected =
            kind.Type == AssetType::Material ? &Assets->MaterialSets : nullptr;
        EXPECT_EQ(kind.ListStore, expected) << kind.Name;
    }
}

// The teardown hazard, exercised: a resident material set releases its members
// into MaterialCache while freeing, so the set's cache has to be destroyed
// first. Declaration order in RuntimeAssets is what guarantees it; this drives
// the path so a reversal is a use-after-free the sanitizer build reports.
TEST_F(AssetComposition, ASetIsFreedWhileTheMaterialsItHoldsStillExist)
{
    constexpr std::string_view kPath = "asset://materials/composition.smat";
    ASSERT_TRUE(Assets->Registry.RegisterOrVerify(AssetRecord{
        .Type = AssetType::Material,
        .SourceKind = AssetSourceKind::Procedural,
        .Path = std::string(kPath),
    }));

    const MaterialHandle material = Assets->Materials.Register(kPath, Material{});
    ASSERT_TRUE(material.IsValid());

    const std::array<std::uint64_t, 1> members{ material.ToToken() };
    AssetLease set = Assets->Assets.InternList(AssetType::Material, members);
    ASSERT_TRUE(set.IsValid());

    // The set is now the only holder of the material, and nothing holds the
    // set: exactly the state a shutdown with live content leaves behind.
    Assets->Materials.Release(material);
    (void)set.Relinquish();
    ASSERT_TRUE(Assets->Materials.IsResident(kPath));

    Assets.reset();
}
