#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityRegistry.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/WorldComponentSchema.h>
#include <gameplay_tags/BaselineGameplayTags.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/Responses.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/build/EntityBuildPackage.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZoneStateLifetime.h>
#include <zone/ZoneStateStore.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

struct LevelPatchCharge
{
    int Level = 0;
};

template <>
struct TypeSchema<LevelPatchCharge>
{
    static constexpr std::string_view Name = "level_patch_charge";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('L', 'P', 'C', 'H');
    static auto Fields() { return std::tuple{ MakeField("level", &LevelPatchCharge::Level) }; }
};

template <>
struct ComponentPersistenceOf<LevelPatchCharge>
{
    static constexpr ComponentPersistence Value = ComponentPersistence::Saved;
};

namespace
{
    constexpr ZoneParticipation kLogic{ .Visible = false, .Physics = false, .Logic = true, .Audio = false };
    constexpr ZoneId kVault{ 0x51 };
    const LevelReference kLevel{ .Kind = LevelKind::World, .Name = "keep" };
    constexpr PersistentEntityId kDoor{ 0x10 };
    constexpr PersistentEntityId kGuard{ 0x20 };
    constexpr PersistentEntityId kCrate{ 0x30 };

    struct Lines final : ILogSink
    {
        void Write(LogLevel level, std::string_view, std::string_view message) override
        {
            (level == LogLevel::Error ? Errors : Warnings).emplace_back(message);
        }

        [[nodiscard]] std::size_t Mentioning(const std::vector<std::string>& lines, std::string_view text) const
        {
            return static_cast<std::size_t>(std::ranges::count_if(
                lines, [&](const std::string& line) { return line.find(text) != std::string::npos; }));
        }

        std::vector<std::string> Errors;
        std::vector<std::string> Warnings;
    };

    WorldComponentSchema SchemaWith(ComponentSerializerRegistry& serializers)
    {
        WorldComponentSchema schema;
        ComponentRegistrar registrar(&schema, &serializers, nullptr);
        RegisterEngineComponents(registrar);
        schema.Add<LevelPatchCharge>();
        (void)serializers.Register(std::make_unique<ComponentSerializer<LevelPatchCharge>>());
        schema.Seal();
        return schema;
    }

    class LevelEntityPatch : public ::testing::Test
    {
    protected:
        LevelEntityPatch()
            : Log(Logging.AddSink<Lines>())
            , Context(Logging)
            , Schema(SchemaWith(Serializers))
            , Runtime(Schema)
        {
            World& world = Entities();
            RegisterAbilityKit(world);
            Unsealed = *world.GetResource<GameplayTagRegistry>().RegisterTag("door.unsealed");
            Health = world.GetResource<AttributeRegistry>().RegisterAttribute("Health");
            Armor = world.GetResource<AttributeRegistry>().RegisterAttribute("Armor");
            world.SetResource(ZoneStateSerialization{ .Serializers = &Serializers, .Context = &Context });
        }

        World& Entities() { return Runtime.Entities(); }
        ZoneStateStore& Store() { return Entities().GetResource<ZoneStateStore>(); }
        ZoneStateScope Scope() { return Store().Scope(kLevel, kVault); }

        static void Identify(EntityBuildPackage& package, PackageEntityId local, PersistentEntityId id)
        {
            EXPECT_TRUE(package.AddComponent<PersistentIdComponent>(local, { id }));
            EXPECT_TRUE(package.SetPersistentId(local, id));
        }

        static void Author(EntityBuildPackage& package, PackageEntityId local, ComponentTypeId type,
                           std::string_view record)
        {
            EXPECT_TRUE(package.SetSerializedComponent(local, type, *JsonParse(record)));
        }

        static EntityBuildPackage Vault(int armor = 5, std::optional<ZoneStateForgetting> doorForgotten = {})
        {
            EntityBuildPackage package;
            const PackageEntityId door = package.CreateEntity();
            Identify(package, door, kDoor);
            Author(package, door, ResolveComponentTypeId<Responses>(),
                   R"({"bindings":{"open":{"on":"test.used","once":true},"use":{"on":"test.used"}}})");
            if (doorForgotten.has_value())
            {
                EXPECT_TRUE(package.AddComponent<ZoneStateLifetime>(door, { *doorForgotten }));
            }
            const PackageEntityId guard = package.CreateEntity();
            Identify(package, guard, kGuard);
            Author(package, guard, ResolveComponentTypeId<AttributeSet>(),
                   R"({"attributes":[{"name":"Health","base":100},{"name":"Armor","base":)" + std::to_string(armor)
                       + "}]}");
            Author(package, guard, ResolveComponentTypeId<AbilityGrants>(),
                   R"({"grants":[{"key":"g_strike","ability":"asset://abilities/strike.sdata"}]})");
            const PackageEntityId crate = package.CreateEntity();
            Identify(package, crate, kCrate);
            EXPECT_TRUE(package.AddComponent<LevelPatchCharge>(crate, { 1 }));
            return package;
        }

        void FinishResidency()
        {
            Runtime.FlushLifecycleRequests();
            (void)Runtime.BeginResidencyProcessing();
            Runtime.FinalizeResidencyProcessing();
        }

        void Import(const EntityBuildPackage& package)
        {
            ZoneImportError error;
            ASSERT_TRUE(ImportZonePackage(Runtime, Schema, kVault, package, Serializers, Context, kLogic, &error,
                                          Scope()))
                << error.Message;
            FinishResidency();
        }

        void Detach()
        {
            ASSERT_TRUE(Runtime.RequestDetach(kVault));
            FinishResidency();
        }

        EntityId Resolve(PersistentEntityId id)
        {
            return Entities().GetResource<PersistentEntityIndex>().TryResolve(id);
        }

        bool IsUnsealed(PersistentEntityId id)
        {
            const GameplayTagContainer* tags = std::as_const(Entities()).TryGet<GameplayTagContainer>(Resolve(id));
            return tags != nullptr && tags->HasExact(Unsealed);
        }

        std::string PatchText(PersistentEntityId id)
        {
            const JsonValue* patch = Store().PatchOf(Scope(), id);
            return patch != nullptr ? JsonStringify(*patch) : std::string();
        }

        void GrantRun(EntityId guard)
        {
            World& world = Entities();
            const AbilityId run = world.GetResource<AbilityRegistry>().Reserve("asset://abilities/run.sdata");
            AbilityGrantEntry declared;
            declared.Key = "g_run";
            declared.Ability = run;
            const std::uint16_t entry = world.GetResource<AbilityGrantEntries>().Intern(std::move(declared));
            ASSERT_TRUE(GrantAbility(world, guard, run, AbilityGrantKind::Request, entry).IsValid());
        }

        bool HoldsRequestedRun(EntityId guard)
        {
            const AbilityGrantEntries& entries = Entities().GetResource<AbilityGrantEntries>();
            return std::ranges::any_of(AbilityGrantsOf(Entities(), guard), [&](const AbilityGrant& grant) {
                const AbilityGrantEntry* entry = entries.Find(grant.Entry);
                return grant.Kind == AbilityGrantKind::Request && entry != nullptr && entry->Key == "g_run";
            });
        }

        LoggingProvider Logging;
        Lines& Log;
        SceneSerializationContext Context;
        ComponentSerializerRegistry Serializers;
        WorldComponentSchema Schema;
        RuntimeWorld Runtime;
        GameplayTagId Unsealed;
        AttributeId Health;
        AttributeId Armor;
    };
}

TEST_F(LevelEntityPatch, ATagGrantedToALevelEntitySurvivesItsZonesRoundTrip)
{
    Import(Vault());
    ASSERT_FALSE(IsUnsealed(kDoor));
    ASSERT_EQ(GrantBaselineTag(Entities(), Resolve(kDoor), Unsealed), GameplayTagWrite::Added);
    Detach();
    ASSERT_FALSE(Resolve(kDoor).IsValid());

    Import(Vault());
    EXPECT_TRUE(IsUnsealed(kDoor)) << "the door forgot it was unsealed: " << PatchText(kDoor);
}

TEST_F(LevelEntityPatch, AnAttributeBaseAndARunTimeGrantSurviveTheRoundTrip)
{
    Import(Vault());
    EntityId guard = Resolve(kGuard);
    ASSERT_TRUE(Entities().TryGet<AttributeSet>(guard)->SetBase(Health, 40.0f));
    GrantRun(guard);
    Detach();

    Import(Vault());
    guard = Resolve(kGuard);
    EXPECT_FLOAT_EQ(std::as_const(Entities()).TryGet<AttributeSet>(guard)->GetBase(Health), 40.0f);
    EXPECT_TRUE(HoldsRequestedRun(guard)) << PatchText(kGuard);
}

TEST_F(LevelEntityPatch, ConsumedAndDisabledBindingsSurviveTheRoundTrip)
{
    Import(Vault());
    Responses* state = Entities().TryGet<Responses>(Resolve(kDoor));
    state->Consumed |= 1u;
    state->Disabled |= 2u;
    Detach();
    EXPECT_EQ(PatchText(kDoor), R"({"responses":{"bindings":{"open":{"consumed":true},"use":{"enabled":false}}}})");

    Import(Vault());
    const Responses* back = std::as_const(Entities()).TryGet<Responses>(Resolve(kDoor));
    EXPECT_EQ(back->Consumed, 1u);
    EXPECT_EQ(back->Disabled, 2u);
}

TEST_F(LevelEntityPatch, AnAtDetachPatchGoesAtDetachAndAnAtRestoreOneAtRestore)
{
    Import(Vault(5, ZoneStateForgetting::AtDetach));
    (void)GrantBaselineTag(Entities(), Resolve(kDoor), Unsealed);
    Detach();
    Import(Vault(5, ZoneStateForgetting::AtDetach));
    EXPECT_FALSE(IsUnsealed(kDoor)) << "forgotten when its zone unloads";
    Detach();

    Import(Vault(5, ZoneStateForgetting::AtRestore));
    (void)GrantBaselineTag(Entities(), Resolve(kDoor), Unsealed);
    Detach();
    Import(Vault(5, ZoneStateForgetting::AtRestore));
    EXPECT_TRUE(IsUnsealed(kDoor)) << "kept across its zone's unload";
    Detach();

    Store().Install(kLevel, Store().ExportDurable(kLevel));
    Import(Vault(5, ZoneStateForgetting::AtRestore));
    EXPECT_FALSE(IsUnsealed(kDoor)) << "forgotten when a save is restored";
}

TEST_F(LevelEntityPatch, ARetunedLevelReachesWhatTheSaveDidNotChange)
{
    Import(Vault(5));
    ASSERT_TRUE(Entities().TryGet<AttributeSet>(Resolve(kGuard))->SetBase(Health, 40.0f));
    Detach();

    Import(Vault(9));
    const AttributeSet* attributes = std::as_const(Entities()).TryGet<AttributeSet>(Resolve(kGuard));
    EXPECT_FLOAT_EQ(attributes->GetBase(Health), 40.0f);
    EXPECT_FLOAT_EQ(attributes->GetBase(Armor), 9.0f) << "the retune reaches a field the save did not change";
}

TEST_F(LevelEntityPatch, AMisfitMemberFollowsContentIsLoggedAndLeavesTheStore)
{
    Store().RecordPatch(Scope(), kGuard,
                        *JsonParse(R"({"AbilityGrants":{"grants":{"g_strike":5}},)"
                                   R"("Attributes":{"attributes":{"Health":{"base":40}}}})"));
    Import(Vault());
    const EntityId guard = Resolve(kGuard);
    ASSERT_TRUE(guard.IsValid()) << "a zone loads whatever its saved state says";
    EXPECT_FLOAT_EQ(std::as_const(Entities()).TryGet<AttributeSet>(guard)->GetBase(Health), 40.0f)
        << "the member that fits applies";
    EXPECT_EQ(AbilityGrantsOf(Entities(), guard).size(), 1u) << "the misfit member follows content";
    EXPECT_EQ(Log.Mentioning(Log.Errors, PersistentEntityIdToString(kGuard)), 1u);
    EXPECT_EQ(Log.Mentioning(Log.Errors, "AbilityGrants"), 1u);
    EXPECT_EQ(PatchText(kGuard), R"({"Attributes":{"attributes":{"Health":{"base":40}}}})");
}

TEST_F(LevelEntityPatch, AMemberNamingAComponentThisBuildLacksSurvivesARewrite)
{
    Store().RecordPatch(Scope(), kDoor, *JsonParse(R"({"future_lock":{"pins":3}})"));
    Import(Vault());
    (void)GrantBaselineTag(Entities(), Resolve(kDoor), Unsealed);
    Detach();
    const std::string kept = PatchText(kDoor);
    EXPECT_NE(kept.find(R"("future_lock":{"pins":3})"), std::string::npos) << kept;
    EXPECT_NE(kept.find("door.unsealed"), std::string::npos) << kept;
}

TEST_F(LevelEntityPatch, AComponentCookedAsBytesIsNotDiffedAndIsLoggedOncePerZoneAndType)
{
    EntityBuildPackage package = Vault();
    const PackageEntityId second = package.CreateEntity();
    Identify(package, second, PersistentEntityId{ 0x31 });
    ASSERT_TRUE(package.AddComponent<LevelPatchCharge>(second, { 2 }));
    Import(package);
    EXPECT_EQ(Log.Mentioning(Log.Warnings, "level_patch_charge"), 1u);
    Entities().TryGet<LevelPatchCharge>(Resolve(kCrate))->Level = 7;
    Detach();
    EXPECT_EQ(PatchText(kCrate), "");
}

TEST_F(LevelEntityPatch, AClientImportsAsAuthoredAndItsDetachRecordsNoPatch)
{
    Entities().SetResource(SimulationAuthority{ .Authoritative = false, .TickOffset = 0 });
    Store().RecordPatch(Scope(), kGuard, *JsonParse(R"({"Attributes":{"attributes":{"Health":{"base":40}}}})"));
    Import(Vault());
    EXPECT_FLOAT_EQ(std::as_const(Entities()).TryGet<AttributeSet>(Resolve(kGuard))->GetBase(Health), 100.0f);
    (void)GrantBaselineTag(Entities(), Resolve(kDoor), Unsealed);
    Detach();
    EXPECT_EQ(PatchText(kDoor), "");
}

TEST_F(LevelEntityPatch, AGameThatKeepsNoLevelStateRemembersOnlyWhatWasDestroyed)
{
    Entities().SetResource(ZoneStateSerialization{});
    Import(Vault());
    (void)GrantBaselineTag(Entities(), Resolve(kDoor), Unsealed);
    Entities().DestroyEntity(Resolve(kCrate));
    Detach();
    EXPECT_EQ(PatchText(kDoor), "");
    EXPECT_TRUE(Store().IsRecordedDestroyed(Scope(), kCrate));

    Import(Vault());
    EXPECT_FALSE(IsUnsealed(kDoor));
    EXPECT_FALSE(Resolve(kCrate).IsValid());
}
