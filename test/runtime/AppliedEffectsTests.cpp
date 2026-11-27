#include "GeneratedVerbCall.h"
#include <abilities/AbilityKit.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <effects/ActiveEffect.h>
#include <effects/AppliedEffects.h>
#include <effects/EffectApplicationLog.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <effects/EffectVerbs.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <runtime/spawn/SceneSpawnOverlay.h>
#include <time/SimClock.h>
#include <world/build/EntityBuildPackage.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/EntityPatch.h>
#include <zone/ZoneStateStore.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
    constexpr double kTick = 1.0 / 60.0;

    int Admitted = 0;
    int Hooked = 0;

    struct EffectsWorld
    {
        explicit EffectsWorld(bool keepsLevelState = true)
            : Context(Logging)
        {
            RegisterAbilityKit(Entities);
            Entities.RegisterComponent<PersistentIdComponent>();
            Entities.AddResource<PersistentEntityIndex>();
            if (keepsLevelState)
                Entities.SetResource(ZoneStateSerialization{ .Serializers = &Serializers, .Context = &Context });
            (void)Serializers.Register(MakeAppliedEffectsSerializer());
            Partitions.Add(StoragePartitionId::Default());
            GameplayTagRegistry& tags = Entities.GetResource<GameplayTagRegistry>();
            Burning = *tags.RegisterTag("test.state.burning");
            Rest = *tags.RegisterTag("test.kind.rest");
            Story = *tags.RegisterTag("test.kind.story");
        }

        EffectId Define(std::string_view name, float seconds, GameplayTagId kind = {})
        {
            EffectDefinition definition;
            definition.Duration = seconds > 0.0f ? EffectDuration::Duration : EffectDuration::Infinite;
            definition.DurationSeconds = seconds;
            definition.GrantedTags = { Burning };
            if (kind.IsValid())
                (void)definition.Kinds.AddExact(kind);
            return Entities.GetResource<EffectRegistry>().Register(name, definition);
        }

        EntityId Identified(std::uint64_t id)
        {
            const EntityId entity = Entities.CreateEntity();
            Entities.AddComponent<PersistentIdComponent>(entity, PersistentIdComponent{ PersistentEntityId{ id } });
            Entities.AddComponent<GameplayTagContainer>(entity, GameplayTagContainer{});
            return entity;
        }

        FixedSimTime Now() const { return FixedSimTime{ .DeltaSeconds = kTick, .TickIndex = Tick }; }

        void Step(int ticks = 1)
        {
            for (int i = 0; i < ticks; ++i)
            {
                ++Tick;
                RestoreAppliedEffects(Entities, Now(), Partitions);
                TickEffects(Entities, Partitions);
            }
        }

        JsonValue Saved(EntityId entity)
        {
            Context.Purpose = SceneRecordPurpose::Save;
            JsonWriteArchive archive;
            EXPECT_TRUE(Serializers.FindByType(ResolveComponentTypeId<AppliedEffects>())->Save(archive, entity,
                                                                                              Entities, Context));
            Context.Purpose = SceneRecordPurpose::Authoring;
            return archive.TakeValue();
        }

        void Load(EntityId entity, const JsonValue& record)
        {
            JsonReadArchive archive(record);
            EXPECT_TRUE(Serializers.FindByType(ResolveComponentTypeId<AppliedEffects>())->LoadIntoWorld(
                archive, entity, Entities, Context));
        }

        std::vector<const ActiveEffect*> On(EntityId target)
        {
            std::vector<const ActiveEffect*> found;
            for (const EntityId entity : Entities.GetAliveEntities())
                if (const ActiveEffect* active = std::as_const(Entities).TryGet<ActiveEffect>(entity);
                    active != nullptr && active->Target == target)
                    found.push_back(active);
            return found;
        }

        LoggingProvider Logging;
        SceneSerializationContext Context;
        ComponentSerializerRegistry Serializers;
        World Entities;
        StoragePartitionSet Partitions;
        std::uint64_t Tick = 100;
        GameplayTagId Burning;
        GameplayTagId Rest;
        GameplayTagId Story;
    };

    std::size_t EntriesIn(const JsonValue& record)
    {
        const JsonValue* effects = record.Find("effects");
        return effects != nullptr && effects->IsArray() ? effects->AsArray().size() : 0;
    }
}

TEST(AppliedEffects, AKeptEffectComesBackWithItsTimeLeftInAWorldWhoseTickDiffersAndTheRowEmptiesOnItsFirstTick)
{
    EffectsWorld writer;
    const EffectId poison = writer.Define("test.poison", 2.0f);
    const EntityId target = writer.Identified(0x11);
    ASSERT_TRUE(ApplyEffect(writer.Entities, target, poison, writer.Now()).IsValid());
    writer.Step(30);
    const std::uint32_t left = writer.On(target).front()->RemainingTicks;
    ASSERT_EQ(SettleAppliedEffects(writer.Entities), 0u);
    const JsonValue saved = writer.Saved(target);
    ASSERT_EQ(EntriesIn(saved), 1u) << JsonStringify(saved);
    EXPECT_EQ(JsonStringify(saved), R"({"effects":[{"key":"test.poison#0","effect":"test.poison","remaining_ticks":)"
                                        + std::to_string(left) + "}]}");

    EffectsWorld reader;
    reader.Tick = 9000;
    (void)reader.Define("test.poison", 2.0f);
    const EntityId restored = reader.Identified(0x11);
    reader.Load(restored, saved);
    ASSERT_EQ(reader.Entities.TryGet<AppliedEffects>(restored)->Count, 1);
    EXPECT_TRUE(reader.On(restored).empty()) << "a row is put back on its entity's first tick, not as it is read";
    reader.Step();
    EXPECT_EQ(reader.Entities.TryGet<AppliedEffects>(restored)->Count, 0) << "the row outlived its first tick";
    const std::vector<const ActiveEffect*> back = reader.On(restored);
    ASSERT_EQ(back.size(), 1u);
    EXPECT_EQ(back.front()->RemainingTicks, left - 1);
    EXPECT_TRUE(back.front()->Kept);
    EXPECT_TRUE(reader.Entities.TryGet<GameplayTagContainer>(restored)->HasExact(reader.Burning));
    reader.Step(5);
    EXPECT_EQ(reader.On(restored).size(), 1u) << "a restored row was put back again";
}

TEST(AppliedEffects, ARestoreRunsNoAdmissionNoApplicationHookAndNoLog)
{
    EffectsWorld world;
    const EffectId poison = world.Define("test.poison", 2.0f);
    const EntityId target = world.Identified(0x12);
    Admitted = 0;
    Hooked = 0;
    world.Entities.SetResource(EffectApplicationAdmission{
        .Admit = [](World&, EntityId, EffectId) { return ++Admitted > 0; },
        .Applied = [](World&, EntityId, EffectId, EntityId) { ++Hooked; } });
    EffectApplicationLog& log = world.Entities.AddResource<EffectApplicationLog>();

    KeptEffect kept;
    kept.Effect = poison;
    kept.RemainingTicks = 40;
    ASSERT_TRUE(RestoreEffect(world.Entities, target, kept, world.Now()).IsValid());
    EXPECT_EQ(Admitted, 0);
    EXPECT_EQ(Hooked, 0);
    EXPECT_TRUE(log.Applications().empty());

    ASSERT_TRUE(ApplyEffect(world.Entities, target, poison, world.Now()).IsValid());
    EXPECT_EQ(Admitted, 1) << "the counters do not see an application";
    EXPECT_EQ(Hooked, 1);
}

TEST(AppliedEffects, OnlyWhatItsApplierKeepsIsKeptAndAnOwnedKeptEffectReturnsWithItsOwnerOnlyWhenResident)
{
    EffectsWorld writer;
    const EffectId poison = writer.Define("test.poison", 2.0f);
    const EntityId target = writer.Identified(0x13);
    const EntityId attacker = writer.Identified(0x14);
    const LeaseOwner owner{ .Entity = attacker, .Key = 7 };
    (void)ApplyEffect(writer.Entities, target, poison, writer.Now(), owner);
    (void)ApplyEffect(writer.Entities, target, poison, writer.Now(), {}, 1.0f, {}, EffectKeeping::NotKept);
    (void)SettleAppliedEffects(writer.Entities);
    EXPECT_FALSE(writer.Entities.HasComponent<AppliedEffects>(target))
        << "what an owner maintains, or a passive applied not kept, was kept";

    (void)ApplyEffect(writer.Entities, target, poison, writer.Now(), owner, 1.0f, {}, EffectKeeping::Kept);
    (void)SettleAppliedEffects(writer.Entities);
    const JsonValue saved = writer.Saved(target);
    ASSERT_EQ(EntriesIn(saved), 1u) << JsonStringify(saved);
    EXPECT_EQ(saved.Find("effects")->AsArray()[0].Find("owner")->AsString(),
              PersistentEntityIdToString(PersistentEntityId{ 0x14 }));

    EffectsWorld withOwner;
    (void)withOwner.Define("test.poison", 2.0f);
    const EntityId resident = withOwner.Identified(0x14);
    const EntityId afflicted = withOwner.Identified(0x13);
    withOwner.Load(afflicted, saved);
    withOwner.Step();
    ASSERT_EQ(withOwner.On(afflicted).size(), 1u);
    EXPECT_EQ(withOwner.On(afflicted).front()->Owner, (LeaseOwner{ .Entity = resident, .Key = 7 }));

    EffectsWorld alone;
    (void)alone.Define("test.poison", 2.0f);
    const EntityId orphan = alone.Identified(0x13);
    alone.Load(orphan, saved);
    alone.Step();
    ASSERT_EQ(alone.On(orphan).size(), 1u) << "an effect whose owner is not resident did not run out its time";
    EXPECT_FALSE(alone.On(orphan).front()->Owner.IsSet());
}

TEST(AppliedEffects, AnEntityNeverUnderAKeptEffectAtACaptureNeverCarriesTheRowNorDoesAnyInAGameThatKeepsNoLevelState)
{
    EffectsWorld world;
    const EffectId brief = world.Define("test.brief", 0.05f);
    const EntityId bystander = world.Identified(0x21);
    const EntityId recovered = world.Identified(0x22);
    (void)ApplyEffect(world.Entities, recovered, brief, world.Now());
    world.Step(10);
    (void)SettleAppliedEffects(world.Entities);
    EXPECT_FALSE(world.Entities.HasComponent<AppliedEffects>(bystander));
    EXPECT_FALSE(world.Entities.HasComponent<AppliedEffects>(recovered))
        << "an effect over before the capture gave its entity the row";

    EffectsWorld unkept(false);
    const EffectId poison = unkept.Define("test.poison", 2.0f);
    const EntityId target = unkept.Identified(0x23);
    (void)ApplyEffect(unkept.Entities, target, poison, unkept.Now());
    EXPECT_EQ(SettleAppliedEffects(unkept.Entities), 0u);
    EXPECT_FALSE(unkept.Entities.HasComponent<AppliedEffects>(target));
    EXPECT_TRUE(OverfullAppliedEffects(unkept.Entities).empty());
}

TEST(AppliedEffects, AFullRowIsReportedAndLeavesOutTheEffectsWithTheLeastTimeLeft)
{
    EffectsWorld world;
    const EntityId target = world.Identified(0x31);
    for (int i = 0; i <= kAppliedEffectCapacity; ++i)
        (void)ApplyEffect(world.Entities, target, world.Define("test.effect." + std::to_string(i), 1.0f + i), world.Now());
    ASSERT_EQ(OverfullAppliedEffects(world.Entities), std::vector<EntityId>{ target });
    EXPECT_EQ(SettleAppliedEffects(world.Entities), 1u);
    const JsonValue saved = world.Saved(target);
    ASSERT_EQ(EntriesIn(saved), kAppliedEffectCapacity);
    for (const JsonValue& entry : saved.Find("effects")->AsArray())
        EXPECT_NE(entry.Find("effect")->AsString(), "test.effect.0") << "the shortest effect was kept";
}

TEST(AppliedEffects, AnAuthoredEffectThatRanOutIsSavedAsRemovedAndIsNotPutBackAgain)
{
    EffectsWorld world;
    (void)world.Define("test.burn", 0.05f);
    const JsonValue authored = JsonValue(JsonValue::Object{
        { "effects", JsonValue(JsonValue::Array{ JsonValue(JsonValue::Object{
                         { "key", JsonValue("test.burn#0") }, { "effect", JsonValue("test.burn") } }) }) } });
    const EntityId barrel = world.Identified(0x41);
    world.Load(barrel, authored);
    world.Step();
    ASSERT_EQ(world.On(barrel).size(), 1u) << "an authored effect was not put on its entity";
    world.Step(10);
    ASSERT_TRUE(world.On(barrel).empty());
    (void)SettleAppliedEffects(world.Entities);

    const std::vector<AuthoredComponent> baseline{ AuthoredComponent{
        .Type = ResolveComponentTypeId<AppliedEffects>(), .Record = authored } };
    const JsonValue patch = CaptureEntityPatch(world.Entities, barrel, baseline, ComponentPersistenceSet{},
                                               world.Serializers, world.Context);
    ASSERT_NE(patch.Find("AppliedEffects"), nullptr) << JsonStringify(patch);

    EntityBuildPackage level;
    const PackageEntityId root = level.CreateEntity();
    ASSERT_TRUE(level.SetSerializedComponent(root, ResolveComponentTypeId<AppliedEffects>(), authored));
    std::string error;
    std::vector<std::string> retuned;
    ASSERT_TRUE(ApplySpawnOverlay(level, SceneSpawnOverlay{ .RootPatch = patch, .RootId = {} }, world.Serializers, error, retuned))
        << error;
    const PackageComponent* composed = level.FindComponent(root, ResolveComponentTypeId<AppliedEffects>());
    ASSERT_TRUE(composed != nullptr && composed->SerializedJson.has_value());
    EXPECT_EQ(EntriesIn(*composed->SerializedJson), 0u) << JsonStringify(*composed->SerializedJson);
}

namespace
{
    struct Removal
    {
        VerbAnswer Answer;
        std::int64_t Count = -1;
    };

    Removal Remove(EffectsWorld& world, EntityId target, AuthoredValue effect, AuthoredValue kinds)
    {
        FixedSimulationLoop clock(kTick);
        EffectOperations operations(world.Entities, clock);
        AuthoredArguments arguments(3);
        arguments.Set(0, AuthoredValue::Entity(target));
        arguments.Set(1, std::move(effect));
        arguments.Set(2, std::move(kinds));
        AuthoredValue result;
        VerbInvocation invocation;
        invocation.Arguments = &arguments;
        invocation.Result = &result;
        Removal removal{ .Answer = CallGeneratedVerb(operations, AuthoredApiDefinition<EffectOperations>::Name_Remove, invocation) };
        (void)result.TryGetInt(removal.Count);
        return removal;
    }

    AuthoredValue AnyOf(std::string_view tag)
    {
        return AuthoredValue::Record({ AuthoredValue::Array({}), AuthoredValue::Array({ AuthoredValue::String(std::string(tag)) }),
                                       AuthoredValue::Array({}) });
    }
}

TEST(AppliedEffects, EffectRemoveEndsOnlyWhatItNamesAndAnswersHowManyAndRefusesNeitherOrBoth)
{
    EffectsWorld world;
    const EffectId cured = world.Define("test.cured", 30.0f, world.Rest);
    const EffectId story = world.Define("test.story", 30.0f, world.Story);
    const EntityId player = world.Identified(0x51);
    (void)ApplyEffect(world.Entities, player, cured, world.Now());
    (void)ApplyEffect(world.Entities, player, cured, world.Now());
    (void)ApplyEffect(world.Entities, player, story, world.Now());

    const Removal neither = Remove(world, player, AuthoredValue{}, AuthoredValue{});
    EXPECT_EQ(neither.Answer.Status, VerbAdmission::Refused);
    EXPECT_EQ(neither.Answer.Detail, static_cast<std::uint16_t>(EffectRemoveRefusal::NothingNamed));
    const Removal both = Remove(world, player, AuthoredValue::DataAsset(AssetRef{ .Type = AssetType::Data, .Path = "test.story" }),
                                AnyOf("test.kind.rest"));
    EXPECT_EQ(both.Answer.Detail, static_cast<std::uint16_t>(EffectRemoveRefusal::BothNamed));
    EXPECT_EQ(world.On(player).size(), 3u) << "a refused removal ended something";

    const Removal rested = Remove(world, player, AuthoredValue{}, AnyOf("test.kind.rest"));
    EXPECT_TRUE(rested.Answer.Accepted());
    EXPECT_EQ(rested.Count, 2);
    ASSERT_EQ(world.On(player).size(), 1u);
    EXPECT_EQ(world.On(player).front()->Def, story) << "an effect of another kind did not survive";

    const Removal named = Remove(world, player, AuthoredValue::DataAsset(AssetRef{ .Type = AssetType::Data, .Path = "test.story" }), AuthoredValue{});
    EXPECT_TRUE(named.Answer.Accepted());
    EXPECT_EQ(named.Count, 1);
    EXPECT_TRUE(world.On(player).empty());
}
