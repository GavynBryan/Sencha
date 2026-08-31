// Who owns a character's movement profile, across every path that can put the
// component on an entity or take it off again.
//
// The profile is a data asset, and the component that names it owns exactly one
// reference to it: taken when the component arrives, dropped when it leaves.
// Everything below is one way that can go wrong -- a hook firing twice, a hook
// not firing at all, a row moving between archetypes, a load that fails
// halfway -- asserted as a balance, because an imbalance is either an asset
// that never frees or a handle pointing at a freed one.

#include <gtest/gtest.h>

#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetLoader.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetKindRegistry.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/DataSchema.h>
#include <ecs/WorldComponentSchema.h>
#include <movement/JumpState.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementComponentSchemas.h>
#include <movement/MovementProfileData.h>
#include <movement/MovementRegistration.h>
#include <net/ReplicationLayout.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializer.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace
{
    constexpr std::string_view kProfilePath = "asset://data/test/walk.sdata";

    constexpr std::string_view kProfileText = R"({
        "type": "movement.profile",
        "version": 1,
        "data": {
            "name": "walk",
            "layers": [ { "name": "Base", "set": { "max_speed": 3.0 } } ]
        }
    })";

    // Everything a movement profile needs to be loadable, and a World whose
    // vocabulary is the engine's own so the component behaves exactly as it
    // does in a running game.
    class MovementProfileLifetimeTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            File = std::filesystem::temp_directory_path()
                / ("sencha_movement_profile_lifetime_"
                   + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".sdata");
            std::ofstream(File, std::ios::trunc) << kProfileText;

            AssetRecord record;
            record.Type = AssetType::Data;
            record.SourceKind = AssetSourceKind::File;
            record.Path = std::string(kProfilePath);
            record.FilePath = File.generic_string();
            ASSERT_TRUE(AssetRegistry_.Register(record));

            RegisterMovementProfileData(DataTypes, DataSchemas);

            AssetKindRegistration data = MakeBuiltinAssetKind(AssetType::Data);
            data.Stager = &DataLoader;
            data.Store = &DataAssets;
            data.Commit = [this](AssetStaging&& staged) -> AssetLease
            {
                const DataAssetHandle handle = DataLoader.CommitTyped(std::move(staged));
                if (!handle.IsValid())
                    return {};
                return AssetLease::Adopt(AssetType::Data, DataAssets, handle.ToToken());
            };
            ASSERT_TRUE(Assets.Kinds().Register(std::move(data)));

            ComponentRegistrar components(World_);
            RegisterEngineComponents(components);
            RegisterMovement(World_);
            World_.AddResource<MovementComponentAssets>(&DataAssets);

            RegisterEngineSceneSerializers(Serializers);
        }

        void TearDown() override
        {
            std::error_code ec;
            std::filesystem::remove(File, ec);
        }

        [[nodiscard]] bool ProfileResident() const
        {
            return DataAssets.Find(kProfilePath).IsValid();
        }

        // A handle the caller owns a reference to, exactly as a producer would
        // hand one over.
        [[nodiscard]] AssetLease LoadProfile()
        {
            return Assets.LoadLease(kProfilePath, AssetType::Data);
        }

        [[nodiscard]] MovementTuningSource SourceFrom(const AssetLease& lease) const
        {
            return MovementTuningSource{ MovementProfileHandle{
                DataAssetHandle::FromToken(lease.OpaqueToken()) } };
        }

        std::filesystem::path File;
        LoggingProvider Logging;
        AssetRegistry AssetRegistry_{ Logging };
        DataAssetTypeRegistry DataTypes;
        DataSchemaRegistry DataSchemas;
        DataAssetCache DataAssets;
        DataAssetLoader DataLoader{ Logging, &DataTypes, &DataSchemas, &DataAssets };
        AssetSystem Assets{ Logging, AssetRegistry_, nullptr, nullptr };
        World World_;
        ComponentSerializerRegistry Serializers;
    };
}

TEST_F(MovementProfileLifetimeTest, AddingAndRemovingTheComponentBalances)
{
    const EntityId entity = World_.CreateEntity();
    {
        AssetLease lease = LoadProfile();
        ASSERT_TRUE(lease.IsValid());
        World_.AddComponent<MovementTuningSource>(entity, SourceFrom(lease));
    }
    EXPECT_TRUE(ProfileResident()) << "the component holds it now";

    World_.RemoveComponent<MovementTuningSource>(entity);
    EXPECT_FALSE(ProfileResident());
}

TEST_F(MovementProfileLifetimeTest, DestroyingTheEntityBalances)
{
    const EntityId entity = World_.CreateEntity();
    {
        AssetLease lease = LoadProfile();
        World_.AddComponent<MovementTuningSource>(entity, SourceFrom(lease));
    }
    ASSERT_TRUE(ProfileResident());

    World_.DestroyEntity(entity);
    EXPECT_FALSE(ProfileResident());
}

// Adding and removing a sibling moves the row to a different archetype. The
// row is copied, not re-created, so the profile's hooks must not fire for a
// move -- a double retain would pin the asset, a double release would free it
// under a live component.
TEST_F(MovementProfileLifetimeTest, MigratingBetweenArchetypesFiresNoHook)
{
    const EntityId entity = World_.CreateEntity();
    {
        AssetLease lease = LoadProfile();
        World_.AddComponent<MovementTuningSource>(entity, SourceFrom(lease));
    }

    for (int i = 0; i < 4; ++i)
    {
        World_.AddComponent<JumpState>(entity, JumpState{});
        World_.RemoveComponent<JumpState>(entity);
    }
    EXPECT_TRUE(ProfileResident()) << "a move released what the row still holds";

    World_.DestroyEntity(entity);
    EXPECT_FALSE(ProfileResident())
        << "a move retained a second reference nothing owns";
}

// The batch import path builds the row at its final signature and writes the
// component in place. OnAdd has to fire there exactly once, or an imported
// character holds a handle nothing keeps alive.
TEST_F(MovementProfileLifetimeTest, AnInPlaceInitializeTakesTheReferenceOnce)
{
    ArchetypeSignature signature;
    signature.set(World_.GetComponentId<MovementTuningSource>());
    const EntityId entity = World_.CreateEntityWithSignature(signature);
    ASSERT_TRUE(World_.HasComponent<MovementTuningSource>(entity));

    {
        AssetLease lease = LoadProfile();
        ASSERT_TRUE(World_.InitializeComponent<MovementTuningSource>(
            entity, SourceFrom(lease)));
    }
    EXPECT_TRUE(ProfileResident());

    World_.DestroyEntity(entity);
    EXPECT_FALSE(ProfileResident());
}

// Tearing the world down is the last chance to let go, and it has to take it:
// a World destroyed with live characters in it must not leave their profiles
// behind.
TEST_F(MovementProfileLifetimeTest, WorldTeardownReleasesWhatItsEntitiesHold)
{
    {
        World scoped;
        WorldComponentSchema schema;
        ComponentRegistrar components(&schema, nullptr, nullptr);
        RegisterEngineComponents(components);
        schema.Seal();
        schema.Apply(scoped);
        scoped.AddResource<MovementComponentAssets>(&DataAssets);

        const EntityId entity = scoped.CreateEntity();
        AssetLease lease = LoadProfile();
        scoped.AddComponent<MovementTuningSource>(entity, SourceFrom(lease));
        lease.Reset();
        ASSERT_TRUE(ProfileResident());
    }

    EXPECT_FALSE(ProfileResident());
}

// Hot reload swaps the value under a live handle rather than minting a new
// one, so what a character already holds keeps being the profile it named.
TEST_F(MovementProfileLifetimeTest, HotReloadKeepsTheHandleTheComponentHolds)
{
    const EntityId entity = World_.CreateEntity();
    DataAssetHandle held;
    {
        AssetLease lease = LoadProfile();
        held = DataAssetHandle::FromToken(lease.OpaqueToken());
        World_.AddComponent<MovementTuningSource>(entity, SourceFrom(lease));
    }

    std::ofstream(File, std::ios::trunc) << R"({
        "type": "movement.profile",
        "version": 1,
        "data": {
            "name": "walk",
            "layers": [ { "name": "Base", "set": { "max_speed": 9.0 } } ]
        }
    })";
    AssetRecord record;
    record.Type = AssetType::Data;
    record.SourceKind = AssetSourceKind::File;
    record.Path = std::string(kProfilePath);
    record.FilePath = File.generic_string();
    ASSERT_TRUE(DataLoader.CommitReload(DataLoader.LoadStaged(record, Assets.DefaultSource())));

    EXPECT_EQ(World_.TryGet<MovementTuningSource>(entity)->Profile.Value, held)
        << "a reload minted a new handle and stranded the component on the old one";
    const auto* reloaded =
        DataAssets.TryGet<CompiledMovementProfile>(held, "movement.profile");
    ASSERT_NE(reloaded, nullptr);

    World_.DestroyEntity(entity);
    EXPECT_FALSE(ProfileResident());
}

// Content names the profile by path; the load's own reference is spent once
// the component owns one.
TEST_F(MovementProfileLifetimeTest, ASceneLoadLeavesTheComponentHoldingItAlone)
{
    const auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "MovementTuning": { "profile": "asset://data/test/walk.sdata" }
                }
            }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    Registry scene;
    ComponentRegistrar components(scene.Components);
    RegisterEngineComponents(components);
    RegisterMovement(scene.Components);
    scene.Components.AddResource<MovementComponentAssets>(&DataAssets);

    SceneSerializationContext context(Logging, &Assets);
    ASSERT_TRUE(LoadSceneJson(*parsed, scene, Serializers, context));
    ASSERT_EQ(scene.Components.CountComponents<MovementTuningSource>(), 1u);
    EXPECT_TRUE(ProfileResident());

    EntityId loaded{};
    scene.Components.ForEachComponent<MovementTuningSource>(
        [&](EntityId entity, const MovementTuningSource& source)
        {
            loaded = entity;
            EXPECT_TRUE(source.Profile.IsValid());
        });
    ASSERT_TRUE(loaded.IsValid());

    scene.Components.DestroyEntity(loaded);
    EXPECT_FALSE(ProfileResident()) << "the load's reference outlived the load";
}

// Authoring writes a bare path; the cook rewrites it as a stamped {id, path}
// so the reference survives a rename. Both shapes have to read, or a prefab
// works in the editor and arrives empty in the game.
TEST_F(MovementProfileLifetimeTest, TheCooksStampedReferenceReadsBack)
{
    const auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "MovementTuning": {
                        "profile": {
                            "id": "c8fe668a5b00d14d",
                            "path": "asset://data/test/walk.sdata"
                        }
                    }
                }
            }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    Registry scene;
    ComponentRegistrar components(scene.Components);
    RegisterEngineComponents(components);
    RegisterMovement(scene.Components);
    scene.Components.AddResource<MovementComponentAssets>(&DataAssets);

    SceneSerializationContext context(Logging, &Assets);
    ASSERT_TRUE(LoadSceneJson(*parsed, scene, Serializers, context));
    ASSERT_EQ(scene.Components.CountComponents<MovementTuningSource>(), 1u);
    scene.Components.ForEachComponent<MovementTuningSource>(
        [](EntityId, const MovementTuningSource& source)
        {
            EXPECT_TRUE(source.Profile.IsValid())
                << "a stamped reference read as nothing";
        });
    EXPECT_TRUE(ProfileResident());
}

// A profile that will not load is a character on default coefficients, not a
// refused scene -- and nothing is acquired for it.
TEST_F(MovementProfileLifetimeTest, AMissingProfileLoadsAsAnUnauthoredCharacter)
{
    const auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "MovementTuning": { "profile": "asset://data/test/absent.sdata" }
                }
            }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    Registry scene;
    ComponentRegistrar components(scene.Components);
    RegisterEngineComponents(components);
    RegisterMovement(scene.Components);
    scene.Components.AddResource<MovementComponentAssets>(&DataAssets);

    SceneSerializationContext context(Logging, &Assets);
    ASSERT_TRUE(LoadSceneJson(*parsed, scene, Serializers, context))
        << "an unresolvable profile refused the whole scene";
    ASSERT_EQ(scene.Components.CountComponents<MovementTuningSource>(), 1u);
    scene.Components.ForEachComponent<MovementTuningSource>(
        [](EntityId, const MovementTuningSource& source)
        { EXPECT_FALSE(source.Profile.IsValid()); });
}

// The structural half of the ownership argument, and the reason the profile is
// not on CharacterMovement: a snapshot overwrites component bytes in place and
// fires no hook, so a replicated component can never own a handle. Asserted
// rather than described, because the split is easy to undo by accident.
TEST_F(MovementProfileLifetimeTest, TheProfileIsNotOnTheWire)
{
    WorldComponentSchema schema;
    ComponentSerializerRegistry serializers;
    ReplicationLayout layout;
    ComponentRegistrar registrar(&schema, &serializers, &layout);
    RegisterEngineComponents(registrar);

    EXPECT_EQ(layout.Find(ResolveComponentTypeId<MovementTuningSource>()), nullptr)
        << "a component owning an asset handle cannot be replicated";
    EXPECT_NE(layout.Find(ResolveComponentTypeId<CharacterMovement>()), nullptr)
        << "the mode still travels";
    EXPECT_NE(serializers.FindByType(ResolveComponentTypeId<MovementTuningSource>()),
              nullptr)
        << "and the profile is still authorable";
}
