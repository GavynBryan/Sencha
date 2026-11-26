#include <abilities/AbilityActivations.h>
#include <abilities/AbilityCostProviders.h>
#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityRegistry.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <abilities/AbilitySystem.h>
#include <app/EngineVocabulary.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <authored/AuthoredApi.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <ecs/Query.h>
#include <effects/ActiveEffect.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/logging/ILogSink.h>
#include <core/serialization/Archive.h>
#include <core/serialization/JsonArchive.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryKit.h>
#include <inventory/InventorySerializers.h>
#include <inventory/InventorySystem.h>
#include <inventory/InventoryVocabulary.h>
#include <inventory/ItemCostProvider.h>
#include <inventory/ItemDefinitionData.h>
#include <net/NetReplicationComponents.h>
#include <save/HeldRecordSaves.h>
#include <time/ActorTimeSystem.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/build/EntityBuildPackage.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/registry/Registry.h>
#include <world/scene/SceneInstance.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZoneStateLifetime.h>
#include <zone/KeptWithZone.h>
#include <zone/ZonePlacement.h>
#include <zone/ZoneStateStore.h>

#include <time/SimClock.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <limits>

namespace
{
struct SceneWorld
{
    Registry Scene;
    LoggingProvider Logging;
    SceneSerializationContext Context{ Logging };

    SceneWorld()
    {
        ComponentRegistrar components(Scene.Components);
        components.Add<PersistentIdComponent>();
        RegisterInventoryComponents(components);
        InstallInventoryVocabulary(Scene.Components);
        Scene.Components.AddResource<GameplayTagRegistry>();
    }

    JsonValue Save(const IComponentSerializer& serializer, EntityId entity)
    {
        JsonWriteArchive archive;
        EXPECT_TRUE(serializer.Save(archive, entity, Scene.Components, Context));
        return archive.TakeValue();
    }
};
}

TEST(InventoryScene, AnEntryIsSavedByItsDefinitionsPathAndItsCount)
{
    SceneWorld first;
    World& world = first.Scene.Components;
    const ItemId arrows = world.GetResource<ItemRegistry>().Reserve("asset://items/arrow.sdata");
    const EntityId stack = world.CreateEntity();
    world.AddComponent<ItemEntry>(stack, ItemEntry{ .Definition = arrows, .Count = 20 });
    const std::unique_ptr<IComponentSerializer> serializer = MakeItemEntrySerializer();
    const JsonValue saved = first.Save(*serializer, stack);

    SceneWorld second;
    World& loaded = second.Scene.Components;
    (void)loaded.GetResource<ItemRegistry>().Reserve("asset://items/other.sdata");
    const EntityId copy = loaded.CreateEntity();
    JsonReadArchive archive(saved);
    ASSERT_TRUE(serializer->LoadIntoWorld(archive, copy, loaded, second.Context));
    const ItemEntry* entry = loaded.TryGet<ItemEntry>(copy);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(loaded.GetResource<ItemRegistry>().GetName(entry->Definition), "asset://items/arrow.sdata");
    EXPECT_EQ(entry->Count, 20u);
}

TEST(InventoryScene, AnEntryLoadsWithItsCountClampedIntoWhatItsStackHolds)
{
    struct Warnings final : ILogSink
    {
        void Write(LogLevel level, std::string_view, std::string_view message) override
        {
            if (level == LogLevel::Warning)
                Lines.emplace_back(message);
        }
        std::vector<std::string> Lines;
    };
    const auto words = [](const std::string& line) {
        std::vector<std::string> found;
        std::string word;
        for (const char c : line + ' ')
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == ':' || c == '/' || c == '.' || c == '_')
                word += c;
            else if (!word.empty())
            {
                while (word.back() == '.' || word.back() == ':')
                    word.pop_back();
                found.push_back(word);
                word.clear();
            }
        }
        return found;
    };
    const auto names = [&](const std::string& line, EntityId entry, std::uint32_t had, std::uint32_t got) {
        const std::vector<std::string> said = words(line);
        const auto has = [&](const std::string& word) { return std::ranges::find(said, word) != said.end(); };
        return has(std::to_string(entry.Index) + ":" + std::to_string(entry.Generation))
            && has("asset://items/arrow.sdata") && has(std::to_string(had)) && has(std::to_string(got));
    };
    const std::unique_ptr<IComponentSerializer> serializer = MakeItemEntrySerializer();

    SceneWorld before;
    World& older = before.Scene.Components;
    const ItemId quiver = older.GetResource<ItemRegistry>().Define("asset://items/arrow.sdata",
                                                                   ItemDefinition{ .Name = "Arrow", .StackMax = 40 });
    const EntityId saved = older.CreateEntity();
    older.AddComponent<ItemEntry>(saved, ItemEntry{ .Definition = quiver, .Count = 37 });
    const JsonValue save = before.Save(*serializer, saved);

    SceneWorld scene;
    Warnings& warned = scene.Logging.AddSink<Warnings>();
    World& world = scene.Scene.Components;
    (void)world.GetResource<ItemRegistry>().Define("asset://items/arrow.sdata",
                                                   ItemDefinition{ .Name = "Arrow", .StackMax = 20 });
    const auto load = [&](const JsonValue& value) {
        const EntityId entry = world.CreateEntity();
        JsonReadArchive archive(value);
        EXPECT_TRUE(serializer->LoadIntoWorld(archive, entry, world, scene.Context));
        return entry;
    };
    const auto authored = [](std::uint32_t count) {
        return JsonValue(JsonValue::Object{ { "definition", JsonValue(std::string("asset://items/arrow.sdata")) },
                                            { "count", JsonValue(static_cast<double>(count)) } });
    };
    const auto countOf = [&](EntityId entry) {
        const ItemEntry* item = std::as_const(world).TryGet<ItemEntry>(entry);
        return item != nullptr ? item->Count : 0u;
    };

    const EntityId full = load(authored(20));
    EXPECT_EQ(countOf(full), 20u);
    EXPECT_TRUE(warned.Lines.empty()) << "a count its stack holds loads as it is";

    const EntityId lowered = load(save);
    EXPECT_EQ(countOf(lowered), 20u) << "a save made before content lowered the stack keeps a full entry";
    ASSERT_EQ(warned.Lines.size(), 1u);
    EXPECT_TRUE(names(warned.Lines[0], lowered, 37, 20)) << warned.Lines[0];

    const EntityId empty = load(authored(0));
    EXPECT_EQ(countOf(empty), 1u) << "an authored entry holds at least one unit";
    ASSERT_EQ(warned.Lines.size(), 2u);
    EXPECT_TRUE(names(warned.Lines[1], empty, 0, 1)) << warned.Lines[1];
}

TEST(InventoryScene, AHeldRecordsHolderIsAReferenceAPlacementRenames)
{
    ComponentSerializerRegistry serializers;
    ComponentRegistrar registrar(nullptr, &serializers, nullptr);
    RegisterEngineComponents(registrar);
    const IComponentSerializer* held = serializers.FindByJsonKey("Held");
    ASSERT_NE(held, nullptr);
    const DataFieldSchema* schema = held->CompositionSchema();
    ASSERT_NE(schema, nullptr);
    const DataFieldSchema* holder = FindChild(*schema, "holder");
    ASSERT_NE(holder, nullptr);
    EXPECT_EQ(holder->Kind, DataFieldKind::Entity);
}

TEST(InventoryScene, AContainerSavesWhatItAcceptsByName)
{
    SceneWorld first;
    World& world = first.Scene.Components;
    const GameplayTagId wearable = *world.GetResource<GameplayTagRegistry>().RegisterTag("Item.Wearable");
    const EntityId wardrobe = world.CreateEntity();
    world.AddComponent<ItemContainer>(wardrobe, ItemContainer{ .Capacity = 6, .Accepts = wearable });
    const std::unique_ptr<IComponentSerializer> serializer = MakeItemContainerSerializer();
    const JsonValue saved = first.Save(*serializer, wardrobe);

    SceneWorld second;
    World& loaded = second.Scene.Components;
    (void)loaded.GetResource<GameplayTagRegistry>().RegisterTag("Item.Other");
    const GameplayTagId there = *loaded.GetResource<GameplayTagRegistry>().RegisterTag("Item.Wearable");
    const EntityId copy = loaded.CreateEntity();
    JsonReadArchive archive(saved);
    ASSERT_TRUE(serializer->LoadIntoWorld(archive, copy, loaded, second.Context));
    EXPECT_EQ(loaded.TryGet<ItemContainer>(copy)->Accepts, there);
    EXPECT_EQ(loaded.TryGet<ItemContainer>(copy)->Capacity, 6u);
}

namespace
{
WorldComponentSchema EngineSchema()
{
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();
    return schema;
}

DataAssetCompileResult CompileItemData(std::string_view json)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    RegisterItemDefinitionData(types, schemas);
    const std::optional<JsonValue> data = JsonParse(json);
    if (!data.has_value())
        return { {}, {}, "not JSON" };
    std::vector<DataValidationError> invalid;
    if (!ValidateDataAgainstSchema(*data, *schemas.Find(kItemDefinitionType), invalid))
        return { {}, {}, FormatDataValidationErrors(invalid) };
    return types.Find(kItemDefinitionType)->Compile(*data);
}

constexpr ZoneParticipation kLogic{ .Visible = false, .Physics = false, .Logic = true, .Audio = false };

struct InventoryWorld
{
    InventoryWorld()
        : Schema(EngineSchema())
        , Runtime(Schema)
    {
        World& world = Entities();
        RegisterAbilityKit(world);
        InstallInventoryVocabulary(world);
        GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
        Wearable = *tags.RegisterTag("Item.Wearable");
        Ammo = world.GetResource<AttributeRegistry>().RegisterAttribute("Ammo", 0.0f, 100.0f, 0.0f);
        EffectDefinition rounds{};
        rounds.Modifiers = { { Ammo, ModifierOp::Add, 18.0f } };
        const EffectId roundsEffect = world.GetResource<EffectRegistry>().Define("asset://effects/rounds.sdata", rounds);

        ItemRegistry& items = world.GetResource<ItemRegistry>();
        Arrows = items.Define("asset://items/arrow.sdata", ItemDefinition{ .Name = "Arrow", .StackMax = 20 });
        Cloak = items.Define("asset://items/cloak.sdata", ItemDefinition{ .Name = "Cloak", .Tags = { Wearable } });
        Clip = items.Define("asset://items/clip.sdata",
                            ItemDefinition{ .Name = "Pistol Ammo", .Keep = false, .TakeEffects = { roundsEffect } });
        Bag = items.Define("asset://items/bag.sdata", ItemDefinition{ .Name = "Bag" });
        Kit = items.Define("asset://items/kit.sdata", ItemDefinition{ .Name = "Kit", .TakeEffects = { roundsEffect } });

        Hero = world.CreateEntity();
        world.AddComponent<ItemContainer>(Hero, ItemContainer{});
        world.AddComponent<NetOwner>(Hero, NetOwner{ .Peer = 1 });
        world.AddComponent<AttributeSet>(Hero);
        world.TryGet<AttributeSet>(Hero)->Add(Ammo, 0.0f);
    }

    World& Entities() { return Runtime.Entities(); }
    const HeldRecordIndex& Index() { return Entities().GetResource<HeldRecordIndex>(); }

    std::vector<EntityId> Held(EntityId container) { return ItemsIn(Entities(), container); }
    std::uint32_t CountOf(EntityId entry) { return Entities().TryGet<ItemEntry>(entry)->Count; }
    PersistentEntityId IdOf(EntityId entity) { return Entities().TryGet<PersistentIdComponent>(entity)->Id; }
    EntityId Resolve(PersistentEntityId id) { return Entities().GetResource<PersistentEntityIndex>().TryResolve(id); }

    EntityId Container(ItemContainer container = {})
    {
        const EntityId entity = Entities().CreateEntity();
        Entities().AddComponent<ItemContainer>(entity, container);
        return entity;
    }

    EntityId Pickup(ItemId definition, std::uint32_t count)
    {
        const EntityId pickup = Entities().CreateEntity();
        Entities().AddComponent<ItemPickup>(pickup, ItemPickup{ .Definition = definition, .Count = count });
        return pickup;
    }

    EntityId BagIn(EntityId holder)
    {
        const EntityId bag = Inventory(Entities(), FixedSimTime{}).Give(holder, Bag, 1).Entry;
        Entities().AddComponent<ItemContainer>(bag, ItemContainer{});
        return bag;
    }

    void FinishResidency()
    {
        Runtime.FlushLifecycleRequests();
        (void)Runtime.BeginResidencyProcessing();
        Runtime.FinalizeResidencyProcessing();
    }

    void Detach(ZoneId zone)
    {
        ASSERT_TRUE(Runtime.RequestDetach(zone));
        FinishResidency();
    }

    ZoneStateScope ScopeOf(ZoneId zone)
    {
        return Entities().GetResource<ZoneStateStore>().Scope(
            LevelReference{ .Kind = LevelKind::Scene, .Name = "inventory" }, zone);
    }

    void Import(ZoneId zone, const EntityBuildPackage& package)
    {
        ZoneImportError error;
        ASSERT_TRUE(ImportZonePackage(Runtime, Schema, zone, package, kLogic, &error, ScopeOf(zone))) << error.Message;
        FinishResidency();
    }

    WorldComponentSchema Schema;
    RuntimeWorld Runtime;
    GameplayTagId Wearable;
    AttributeId Ammo;
    ItemId Arrows;
    ItemId Cloak;
    ItemId Clip;
    ItemId Bag;
    ItemId Kit;
    EntityId Hero;
};

class InventoryTest : public ::testing::Test, public InventoryWorld
{
};

struct Cellar
{
    static constexpr PersistentEntityId kChest{ 0xC4E57 };
    static constexpr PersistentEntityId kSword{ 0x5A0D };

    ZoneId Zone{ 31 };
    ItemId Sword;
    std::uint32_t Count = 1;

    EntityBuildPackage Package() const
    {
        EntityBuildPackage made;
        const PackageEntityId chest = made.CreateEntity();
        EXPECT_TRUE(made.AddComponent<PersistentIdComponent>(chest, { kChest }));
        EXPECT_TRUE(made.SetPersistentId(chest, kChest));
        EXPECT_TRUE(made.AddComponent<ItemContainer>(chest, ItemContainer{}));
        if (!Sword.IsValid())
            return made;
        const PackageEntityId sword = made.CreateEntity();
        EXPECT_TRUE(made.AddComponent<PersistentIdComponent>(sword, { kSword }));
        EXPECT_TRUE(made.SetPersistentId(sword, kSword));
        EXPECT_TRUE(made.AddComponent<ItemEntry>(sword, ItemEntry{ .Definition = Sword, .Count = Count }));
        EXPECT_TRUE(made.AddComponent<::Held>(sword, ::Held{ .Holder = { .Entity = {}, .Target = kChest } }));
        return made;
    }
};
}

TEST_F(InventoryTest, AGivenItemFillsItsStacksBeforeMakingEntries)
{
    Inventory inventory(Entities(), FixedSimTime{});
    ASSERT_EQ(inventory.Give(Hero, Arrows, 30).Refusal, InventoryRefusal::None);
    std::vector<EntityId> held = Held(Hero);
    ASSERT_EQ(held.size(), 2u);
    EXPECT_EQ(CountOf(held[0]), 20u);
    EXPECT_EQ(CountOf(held[1]), 10u);
    for (std::size_t i = 0; i < held.size(); ++i)
    {
        const EntityId entry = held[i];
        EXPECT_EQ(Entities().TryGet<::Held>(entry)->Position, i);
        EXPECT_EQ(Index().HolderOf(entry), Hero);
        EXPECT_FALSE(IsAuthoredPersistentEntityId(IdOf(entry)));
        EXPECT_EQ(Entities().TryGet<NetRelevanceAnchor>(entry)->Anchor, Hero);
        EXPECT_TRUE(Entities().HasComponent<NetReplicated>(entry));
    }

    ASSERT_EQ(inventory.Give(Hero, Arrows, 15).Refusal, InventoryRefusal::None);
    held = Held(Hero);
    ASSERT_EQ(held.size(), 3u);
    EXPECT_EQ(CountOf(held[1]), 20u) << "the open stack filled first";
    EXPECT_EQ(CountOf(held[2]), 5u);
}

TEST_F(InventoryTest, AContainerRefusesWhatItDoesNotAcceptAndWhatDoesNotFit)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId wardrobe = Container({ .Capacity = 2, .Accepts = Wearable });
    EXPECT_EQ(inventory.Give(wardrobe, Arrows, 1).Refusal, InventoryRefusal::NotAccepted);
    EXPECT_EQ(inventory.Give(wardrobe, Cloak, 3).Refusal, InventoryRefusal::ContainerFull) << "all or none";
    EXPECT_TRUE(Held(wardrobe).empty());
    ASSERT_EQ(inventory.Give(wardrobe, Cloak, 2).Refusal, InventoryRefusal::None) << "exactly its capacity fits";
    EXPECT_EQ(inventory.Give(wardrobe, Cloak, 1).Refusal, InventoryRefusal::ContainerFull);
    EXPECT_EQ(Held(wardrobe).size(), 2u) << "a refusal changes nothing";
}

TEST_F(InventoryTest, AContainerWithoutACapacityHoldsAsManyEntriesAsACapacityCanSay)
{
    const ItemId coin = Entities().GetResource<ItemRegistry>().Define("asset://items/coin.sdata",
                                                                      ItemDefinition{ .Name = "Coin" });
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId vault = Container();
    EXPECT_EQ(inventory.Give(vault, coin, std::numeric_limits<std::uint32_t>::max()).Refusal,
              InventoryRefusal::ContainerFull);
    EXPECT_TRUE(Held(vault).empty()) << "an unbounded give makes nothing";
    ASSERT_EQ(inventory.Give(vault, coin, kItemContainerMostEntries).Refusal, InventoryRefusal::None);
    EXPECT_EQ(Held(vault).size(), kItemContainerMostEntries);
    EXPECT_EQ(inventory.Give(vault, coin, 1).Refusal, InventoryRefusal::ContainerFull);
}

TEST_F(InventoryTest, AGiftPastTheLastPositionRenumbersTheContainerAndKeepsItsOrder)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId chest = Container();
    const EntityId first = inventory.Give(chest, Cloak, 1).Entry;
    HoldRecord(Entities(), first, chest, std::uint16_t{ 0xFFFF });
    const std::uint64_t before = Index().PositionsExhausted();

    const EntityId second = inventory.Give(chest, Cloak, 1).Entry;

    EXPECT_EQ(Entities().TryGet<::Held>(first)->Position, 0u);
    EXPECT_EQ(Entities().TryGet<::Held>(second)->Position, 1u);
    EXPECT_EQ(Held(chest), (std::vector{ first, second }));
    EXPECT_EQ(Index().PositionsExhausted(), before);
}

TEST_F(InventoryTest, AnEntryDeeperThanRecordsNestIsRefused)
{
    Inventory inventory(Entities(), FixedSimTime{});
    std::vector<EntityId> bags{ BagIn(Hero) };
    while (static_cast<int>(bags.size()) < kHeldDeepestNesting - 1)
        bags.push_back(BagIn(bags.back()));
    const EntityId deepest = bags.back();
    EXPECT_EQ(inventory.Give(deepest, Cloak, 1).Refusal, InventoryRefusal::None) << "the last level holds";
    EXPECT_EQ(Index().Depth(Held(deepest).front()), kHeldDeepestNesting);

    const EntityId into = Held(deepest).front();
    Entities().AddComponent<ItemContainer>(into, ItemContainer{});
    EXPECT_EQ(inventory.Give(into, Cloak, 1).Refusal, InventoryRefusal::TooDeep);
    const EntityId pouch = BagIn(Hero);
    (void)inventory.Give(pouch, Cloak, 1);
    EXPECT_EQ(inventory.Transfer(pouch, deepest, 0).Refusal, InventoryRefusal::TooDeep)
        << "what the pouch holds would lie past the bound";
}

TEST_F(InventoryTest, TakingAPickupMakesItsEntryAndTakingAmmunitionKeepsNothing)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId quiver = Pickup(Arrows, 12);
    ASSERT_EQ(inventory.Take(quiver, Hero).Refusal, InventoryRefusal::None);
    EXPECT_FALSE(Entities().IsAlive(quiver));
    ASSERT_EQ(Held(Hero).size(), 1u);
    EXPECT_EQ(CountOf(Held(Hero)[0]), 12u);

    const EntityId clip = Pickup(Clip, 1);
    ASSERT_EQ(inventory.Take(clip, Hero).Refusal, InventoryRefusal::None);
    EXPECT_FALSE(Entities().IsAlive(clip));
    EXPECT_EQ(Held(Hero).size(), 1u) << "kept nothing";
    EXPECT_FLOAT_EQ(Entities().TryGet<AttributeSet>(Hero)->GetBase(Ammo), 18.0f);
}

TEST_F(InventoryTest, APickupsTakeEffectsAreScaledByTheUnitsItCounts)
{
    Inventory inventory(Entities(), FixedSimTime{});
    ASSERT_EQ(inventory.Take(Pickup(Clip, 3), Hero).Refusal, InventoryRefusal::None);
    EXPECT_FLOAT_EQ(Entities().TryGet<AttributeSet>(Hero)->GetBase(Ammo), 54.0f);
}

TEST_F(InventoryTest, ATakeEffectIsAppliedInTheTickTheTakeRunsIn)
{
    EffectDefinition lasting{};
    lasting.Duration = EffectDuration::Duration;
    lasting.DurationSeconds = 1.0f;
    lasting.Modifiers = { { Ammo, ModifierOp::Add, 1.0f } };
    const EffectId timed = Entities().GetResource<EffectRegistry>().Define("asset://effects/timed.sdata", lasting);
    const ItemId flare = Entities().GetResource<ItemRegistry>().Define(
        "asset://items/flare.sdata", ItemDefinition{ .Name = "Flare", .Keep = false, .TakeEffects = { timed } });

    Inventory inventory(Entities(), FixedSimTime{ .DeltaSeconds = 1.0 / 30.0, .TickIndex = 7 });
    ASSERT_EQ(inventory.Take(Pickup(flare, 1), Hero).Refusal, InventoryRefusal::None);

    std::vector<EntityId> effects;
    Query<Read<ActiveEffect>> active(Entities());
    active.ForEachChunk([&](auto& view) {
        for (std::uint32_t row = 0; row < view.Count(); ++row)
            effects.push_back(view.Entity(row));
    });
    ASSERT_EQ(effects.size(), 1u);
    EXPECT_EQ(Entities().TryGet<ActiveEffect>(effects[0])->RemainingTicks, 30u) << "a second of thirty-hertz ticks";
}

TEST_F(InventoryTest, TakingBackADroppedEntryAppliesNoTakeEffect)
{
    Inventory inventory(Entities(), FixedSimTime{});
    ASSERT_EQ(inventory.Take(Pickup(Kit, 2), Hero).Refusal, InventoryRefusal::None);
    ASSERT_EQ(Held(Hero).size(), 2u);
    EXPECT_FLOAT_EQ(Entities().TryGet<AttributeSet>(Hero)->GetBase(Ammo), 36.0f) << "kept, and scaled by the units";

    const EntityId dropped = inventory.Drop(Held(Hero)[0], Transform3f::Identity(), StoragePartitionId{ 0 }).Entry;
    ASSERT_EQ(inventory.Take(dropped, Hero).Refusal, InventoryRefusal::None);
    EXPECT_EQ(Held(Hero).size(), 2u);
    EXPECT_FLOAT_EQ(Entities().TryGet<AttributeSet>(Hero)->GetBase(Ammo), 36.0f);
}

TEST_F(InventoryTest, ATransferOfPartOfAStackSplitsIt)
{
    Inventory inventory(Entities(), FixedSimTime{});
    ASSERT_EQ(inventory.Give(Hero, Arrows, 20).Refusal, InventoryRefusal::None);
    const EntityId stack = Held(Hero)[0];
    const PersistentEntityId identity = IdOf(stack);
    const EntityId chest = Container();

    const InventoryResult moved = inventory.Transfer(stack, chest, 5);
    ASSERT_EQ(moved.Refusal, InventoryRefusal::None);
    EXPECT_EQ(CountOf(stack), 15u) << "the remainder keeps its identity";
    EXPECT_EQ(IdOf(stack), identity);
    ASSERT_EQ(Held(chest), std::vector{ moved.Entry });
    EXPECT_EQ(CountOf(moved.Entry), 5u);
    EXPECT_NE(IdOf(moved.Entry), identity);
    EXPECT_EQ(Entities().TryGet<NetRelevanceAnchor>(moved.Entry)->Anchor, chest);

    ASSERT_EQ(inventory.Transfer(stack, chest, 0).Refusal, InventoryRefusal::None);
    ASSERT_EQ(Held(chest).size(), 1u) << "zero moves all of it, into the stack already there";
    EXPECT_EQ(CountOf(Held(chest)[0]), 20u);
    EXPECT_EQ(CountOf(stack), 0u);
    RunInventoryPass(Entities());
    EXPECT_TRUE(Held(Hero).empty()) << "merged away, the stack is spent and reaped";
}

TEST_F(InventoryTest, ADroppedEntryLiesInItsPickupAndIsTakenBackWhole)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId cloak = inventory.Give(Hero, Cloak, 1).Entry;
    const PersistentEntityId identity = IdOf(cloak);

    const InventoryResult dropped = inventory.Drop(cloak, Transform3f::Identity(), StoragePartitionId{ 0 });
    ASSERT_EQ(dropped.Refusal, InventoryRefusal::None);
    const EntityId pickup = dropped.Entry;
    EXPECT_TRUE(Held(Hero).empty());
    EXPECT_EQ(Held(pickup), std::vector{ cloak });
    EXPECT_EQ(Entities().TryGet<NetRelevanceAnchor>(cloak)->Anchor, pickup) << "relevant to nobody while it lies there";

    ASSERT_EQ(inventory.Take(pickup, Hero).Refusal, InventoryRefusal::None);
    EXPECT_FALSE(Entities().IsAlive(pickup));
    EXPECT_EQ(Held(Hero), std::vector{ cloak }) << "the same entry, not a new one";
    EXPECT_EQ(IdOf(cloak), identity);
}

TEST_F(InventoryTest, AnEntryLeavesItsSpawnGroupTheFirstTimeItMoves)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId purse = inventory.Give(Hero, Cloak, 1).Entry;
    Entities().AddComponent<SceneInstance>(purse, SceneInstance{ AssetId{ 0x9 }, SceneInstanceId{ 0x77 } });
    ASSERT_EQ(inventory.Transfer(purse, Container(), 0).Refusal, InventoryRefusal::None);
    EXPECT_FALSE(Entities().HasComponent<SceneInstance>(purse))
        << "its guard's despawn would otherwise take it from the thief";
}

TEST_F(InventoryTest, OnlyTheAuthorityChangesAnInventory)
{
    Entities().SetResource(SimulationAuthority{ .Authoritative = false, .TickOffset = 0 });
    Inventory inventory(Entities(), FixedSimTime{});
    EXPECT_EQ(inventory.Give(Hero, Arrows, 1).Refusal, InventoryRefusal::NotAuthority);
    EXPECT_TRUE(Held(Hero).empty());
}

namespace
{
bool Withhold(const World& world, EntityId entry)
{
    return world.TryGet<ItemEntry>(entry)->Count != 7;
}
}

TEST_F(InventoryTest, ALayerAboveMayWithholdARequestedMoveButNotAForcedOne)
{
    Entities().AddResource<ItemMoveAdmission>(ItemMoveAdmission{ .Admit = &Withhold, .Moved = nullptr });
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId cursed = inventory.Give(Hero, Arrows, 7).Entry;
    const EntityId chest = Container();
    EXPECT_EQ(inventory.Transfer(cursed, chest, 0).Refusal, InventoryRefusal::Withheld);
    EXPECT_EQ(inventory.Drop(cursed, Transform3f::Identity(), StoragePartitionId{ 0 }).Refusal,
              InventoryRefusal::Withheld);
    EXPECT_EQ(inventory.Transfer(cursed, chest, 0, ItemChangeClass::Forced).Refusal, InventoryRefusal::None);
    EXPECT_EQ(Held(chest), std::vector{ cursed });
}

TEST_F(InventoryTest, AStackOfEntriesWithStateOfTheirOwnIsRefused)
{
    ItemDeclaration declaration;
    declaration.Name = "Charged Wand";
    declaration.StackMax = 5;
    declaration.Attributes = { { .Name = "Ammo", .Base = 3.0f } };
    ItemDefinition definition;
    std::vector<std::string> errors;
    EXPECT_FALSE(ResolveItemDeclaration(declaration, Entities(), definition, errors));
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors.front().find("stack.max"), std::string::npos) << errors.front();

    declaration.StackMax = 1;
    errors.clear();
    EXPECT_TRUE(ResolveItemDeclaration(declaration, Entities(), definition, errors));
}

TEST_F(InventoryTest, MoreStateThanAnEntryHoldsIsRefused)
{
    ItemDeclaration declaration;
    declaration.Name = "Relic";
    for (std::size_t i = 0; i < GameplayTagContainer::Capacity; ++i)
        declaration.InstanceTags.push_back(std::string(Entities().GetResource<GameplayTagRegistry>().GetName(
            *Entities().GetResource<GameplayTagRegistry>().RegisterTag("Relic.State" + std::to_string(i)))));
    declaration.Attributes.assign(AttributeSet::Capacity, { .Name = "Ammo", .Base = 0.0f });
    ItemDefinition definition;
    std::vector<std::string> errors;
    EXPECT_TRUE(ResolveItemDeclaration(declaration, Entities(), definition, errors)) << "exactly as much holds";

    declaration.InstanceTags.push_back("Item.Wearable");
    declaration.Attributes.push_back({ .Name = "Ammo", .Base = 0.0f });
    errors.clear();
    EXPECT_FALSE(ResolveItemDeclaration(declaration, Entities(), definition, errors));
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_NE(errors[0].find("at most 32 tags"), std::string::npos) << errors[0];
    EXPECT_NE(errors[1].find("at most 16 attributes"), std::string::npos) << errors[1];
}

namespace
{
constexpr std::string_view kDrink = R"({
    "costs": [ { "key": "g_sip", "resources": [ { "key": "r_unit", "provider": "inventory.item",
                                                  "arguments": { "entry": { "port": "source" } } } ] } ],
    "phases": [ { "key": "p_drink",
        "enter": [ { "key": "o_pay", "op": "commit", "groups": ["g_sip"] } ],
        "then": [ { "key": "t_done", "on": "timeout", "ticks": 3, "to": "end" } ] } ] })";

class InventoryConsumingTest : public InventoryTest
{
protected:
    InventoryConsumingTest()
    {
        World& world = Entities();
        Drink = DefineAbility("asset://abilities/drink.sdata", kDrink);
        Potion = world.GetResource<ItemRegistry>().Define(
            "asset://items/potion.sdata",
            ItemDefinition{ .Name = "Potion", .StackMax = 5,
                            .UseEntry = world.GetResource<AbilityGrantEntries>().Intern({ "use", Drink, {} }) });
        world.AddComponent<AbilityGrants>(Hero, AbilityGrants{});
    }

    AbilityId DefineAbility(std::string_view path, std::string_view json)
    {
        AbilityDeclaration declaration;
        std::vector<AssetRef> dependencies;
        std::vector<AbilityDiagnostic> diagnostics;
        EXPECT_TRUE(CompileAbilityDeclaration(*JsonParse(json), declaration, dependencies, diagnostics));
        AbilityDefinition definition;
        EXPECT_TRUE(ResolveAbilityDeclaration(declaration, path, MakeAbilityResolveEnvironment(Entities()), definition,
                                              diagnostics))
            << (diagnostics.empty() ? std::string() : diagnostics.front().Message);
        return Entities().GetResource<AbilityRegistry>().Define(path, std::move(definition));
    }

    void Step()
    {
        AdvanceActorTime(Entities(), Now);
        RunAbilityPass(Entities(), AbilityHost{}, FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = Now++ });
        RunInventoryPass(Entities());
    }

    AbilityId Drink;
    ItemId Potion;
    std::uint64_t Now = 0;
};

std::vector<AbilityCostRow> Rows(std::span<const std::vector<AuthoredValue>> arguments, EntityId payer)
{
    std::vector<AbilityCostRow> rows;
    for (const std::vector<AuthoredValue>& row : arguments)
        rows.push_back({ .Actor = payer, .Source = {}, .Arguments = row });
    return rows;
}
}

TEST_F(InventoryConsumingTest, AUseTheEntryTableRefusesFailsTheDefinition)
{
    (void)Entities().GetResource<AbilityGrantEntries>().Intern({ "k32728", Drink, {} });
    ItemDeclaration declaration;
    declaration.Name = "Elixir";
    declaration.Use = AbilityGrantDeclaration{ .Key = "k261234", .Ability = "asset://abilities/drink.sdata", .Override = {} };
    ItemDefinition definition;
    std::vector<std::string> errors;
    EXPECT_FALSE(ResolveItemDeclaration(declaration, Entities(), definition, errors));
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors.front().find("k261234"), std::string::npos) << errors.front();
}

TEST_F(InventoryConsumingTest, TheLastUnitIsReapedOnlyOnceTheActivationUsingItEnds)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId potion = inventory.Give(Hero, Potion, 1).Entry;
    ASSERT_EQ(inventory.Use(Hero, potion).Refusal, InventoryRefusal::None);
    Step();
    EXPECT_EQ(CountOf(potion), 0u) << "the sip was paid";
    EXPECT_TRUE(Entities().IsAlive(potion)) << "depleted, not destroyed, while its activation runs";
    EXPECT_TRUE(HasLiveActivationFrom(Entities(), Hero, potion));

    for (int i = 0; i < 6; ++i)
        Step();
    EXPECT_FALSE(HasLiveActivationFrom(Entities(), Hero, potion));
    EXPECT_FALSE(Entities().IsAlive(potion)) << "reaped once nothing runs from it";
    EXPECT_TRUE(Held(Hero).empty());
}

TEST_F(InventoryConsumingTest, AUseSpendsOnlyFromWhatTheActorStillHolds)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId potion = inventory.Give(Hero, Potion, 2).Entry;
    const EntityId friendly = Container();
    ASSERT_EQ(inventory.Use(Hero, potion).Refusal, InventoryRefusal::None);
    ASSERT_EQ(inventory.Transfer(potion, friendly, 0).Refusal, InventoryRefusal::None);
    Step();
    EXPECT_EQ(CountOf(potion), 2u) << "the friend's potions paid for the hero's drink";
    EXPECT_FALSE(HasLiveActivationFrom(Entities(), Hero, potion));
}

TEST_F(InventoryConsumingTest, NobodyUsesWhatTheyDoNotHold)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId potion = inventory.Give(Container(), Potion, 1).Entry;
    EXPECT_EQ(inventory.Use(Hero, potion).Refusal, InventoryRefusal::NotPermitted);
    EXPECT_FALSE(MayUseItem(Entities(), Hero, potion));
    const EntityId bag = BagIn(Hero);
    const EntityId carried = inventory.Give(bag, Potion, 1).Entry;
    EXPECT_TRUE(MayUseItem(Entities(), Hero, carried)) << "what a bag holds is its holder's";
    EXPECT_TRUE(MayUseItem(Entities(), Hero, Hero)) << "an actor uses what it holds itself";
}

TEST_F(InventoryConsumingTest, ADepletedEntryIsNeverRefilled)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId potion = inventory.Give(Hero, Potion, 1).Entry;
    ASSERT_EQ(inventory.Use(Hero, potion).Refusal, InventoryRefusal::None);
    Step();
    ASSERT_EQ(CountOf(potion), 0u);
    ASSERT_EQ(inventory.Give(Hero, Potion, 2).Refusal, InventoryRefusal::None);
    EXPECT_EQ(CountOf(potion), 0u);
    EXPECT_EQ(Held(Hero).size(), 2u) << "a new entry, beside the spent one";
}

TEST_F(InventoryTest, TwoRowsTakingOneArrowEachCheckForTwo)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId arrow = inventory.Give(Hero, Arrows, 1).Entry;
    const std::vector<std::vector<AuthoredValue>> one{ { AuthoredValue::Entity(arrow), {}, AuthoredValue::Int(1) } };
    const std::vector<std::vector<AuthoredValue>> two{ { AuthoredValue::Entity(arrow), {}, AuthoredValue::Int(1) },
                                                       { AuthoredValue::Entity(arrow), {}, AuthoredValue::Int(1) } };
    EXPECT_TRUE(CheckItemCosts(Entities(), Rows(one, Hero)).Affordable);
    const AbilityCostProviderCheck both = CheckItemCosts(Entities(), Rows(two, Hero));
    EXPECT_FALSE(both.Affordable);
    EXPECT_EQ(both.Row, 1u);
}

TEST_F(InventoryTest, ABottlesContentsAreSpentAndTheBottleStays)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const ItemId fairy = Entities().GetResource<ItemRegistry>().Define("asset://items/fairy.sdata",
                                                                       ItemDefinition{ .Name = "Fairy" });
    const EntityId bottle = BagIn(Hero);
    ASSERT_EQ(inventory.Give(bottle, fairy, 1).Refusal, InventoryRefusal::None);
    const EntityId contents = Held(bottle)[0];
    EXPECT_EQ(Entities().TryGet<NetRelevanceAnchor>(contents)->Anchor, bottle);

    const std::vector<std::vector<AuthoredValue>> sip{ { {}, AuthoredValue::Entity(bottle), AuthoredValue::Int(1) } };
    const std::vector<AbilityCostRow> rows = Rows(sip, Hero);
    ASSERT_TRUE(CheckItemCosts(Entities(), rows).Affordable);
    ApplyItemCosts(Entities(), rows);
    EXPECT_EQ(CountOf(contents), 0u);
    EXPECT_EQ(CountOf(bottle), 1u) << "an empty bottle is still a bottle";
    EXPECT_FALSE(CheckItemCosts(Entities(), rows).Affordable) << "a spent fairy cannot be drunk again";
    const std::vector<std::vector<AuthoredValue>> stranger{ { AuthoredValue::Entity(contents), {}, AuthoredValue::Int(1) } };
    EXPECT_FALSE(CheckItemCosts(Entities(), Rows(stranger, Entities().CreateEntity())).Affordable)
        << "only the holder pays with what it holds";
}

TEST_F(InventoryTest, WhatAnAuthoredChestHoldsIsListedUnderItWhenRecordsSettle)
{
    const EntityId chest = Container();
    Entities().AddComponent<PersistentIdComponent>(chest, PersistentIdComponent{ Cellar::kChest });
    const EntityId authored = Entities().CreateEntity();
    Entities().AddComponent<ItemEntry>(authored, ItemEntry{ .Definition = Arrows, .Count = 7 });
    Entities().AddComponent<::Held>(authored, ::Held{ .Holder = { .Entity = {}, .Target = Cellar::kChest } });
    SettleHeldRecords(Entities());
    EXPECT_EQ(Held(chest), std::vector{ authored });
    EXPECT_EQ(Entities().TryGet<NetRelevanceAnchor>(authored)->Anchor, chest);
    EXPECT_TRUE(Entities().HasComponent<NetReplicated>(authored));
}

TEST_F(InventoryTest, WhatADestroyedContainerHeldGoesWithIt)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId chest = Container();
    const EntityId arrows = inventory.Give(chest, Arrows, 3).Entry;
    Entities().DestroyEntity(chest);
    SettleHeldRecords(Entities());
    EXPECT_FALSE(Entities().IsAlive(arrows));
}

namespace
{
class InventoryVocabularyTest : public InventoryTest
{
protected:
    InventoryVocabularyTest()
    {
        InstallAuthoredVocabulary(Entities());
        EXPECT_TRUE(InstallEngineVocabulary(Entities()));
        Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(Entities()));
        Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(Entities()));
        const VerbRegistry& catalog = Verbs->Registry();
        Tokens.push_back(Verbs->Bind<&InvokeInventoryTake>(catalog.Find(kInventoryTakeVerb), Operations));
        Tokens.push_back(Verbs->Bind<&InvokeInventoryGive>(catalog.Find(kInventoryGiveVerb), Operations));
        Tokens.push_back(Verbs->Bind<&InvokeInventoryTransfer>(catalog.Find(kInventoryTransferVerb), Operations));
        Tokens.push_back(Verbs->Bind<&InvokeInventoryDrop>(catalog.Find(kInventoryDropVerb), Operations));
        QueryToken = BindInventoryQuery(*Queries, std::as_const(Runtime).Entities());
        Searches = BindAuthoredApi(nullptr, Queries.get(), Operations);
    }

    VerbAdmission Invoke(std::string_view verb, std::vector<std::pair<std::string, EntityId>> entities,
                         std::vector<VerbBindingArgument> literals = {})
    {
        VerbBindingDesc desc;
        desc.Key = std::string(verb) + ".test";
        desc.KeyId = MakeVerbBindingKey(desc.Key);
        desc.VerbName = std::string(verb);
        std::vector<AuthoredValue> inputs;
        for (const auto& [name, entity] : entities)
        {
            desc.Inputs.push_back(name);
            VerbBindingArgument input;
            input.Key = name;
            input.Source = VerbArgumentSource::Input;
            input.Text = name;
            desc.Arguments.push_back(std::move(input));
            inputs.push_back(AuthoredValue::Entity(entity));
        }
        for (VerbBindingArgument& literal : literals)
            desc.Arguments.push_back(std::move(literal));
        CompiledVerbBinding binding;
        std::vector<std::string> errors;
        EXPECT_TRUE(CompileVerbBinding(desc, MakeVerbBindingEnvironment(Entities()), binding, errors))
            << (errors.empty() ? "" : errors.front());
        const VerbInvocationResult result = Verbs->Invoke(binding, inputs);
        Last = { binding.Verb, VerbAnswer(result.Status, result.Detail) };
        return result.Status;
    }

    [[nodiscard]] std::string LastAnswer() const { return DescribeVerbAnswer(Verbs->Registry(), Last.first, Last.second); }

    static VerbBindingArgument Definition(std::string path)
    {
        VerbBindingArgument argument;
        argument.Key = "definition";
        argument.Source = VerbArgumentSource::DataAsset;
        argument.Text = std::move(path);
        return argument;
    }

    static VerbBindingArgument Count(double count)
    {
        VerbBindingArgument argument;
        argument.Key = "count";
        argument.Literal = JsonValue(count);
        return argument;
    }

    std::int64_t CountIn(EntityId container, AuthoredValue definition, AuthoredValue tag = {}, bool within = false)
    {
        AuthoredValue result;
        const std::vector<AuthoredValue> arguments{ AuthoredValue::Entity(container), definition, tag,
                                                    AuthoredValue::Bool(within) };
        EXPECT_EQ(Queries->Evaluate(Queries->Registry().Resolve(kInventoryCountQuery), arguments, result),
                  AuthoredQueryStatus::Value);
        std::int64_t count = -1;
        EXPECT_TRUE(result.TryGetInt(count));
        return count;
    }

    std::optional<EntityId> EntryIn(EntityId container, AuthoredValue definition, AuthoredValue tag = {},
                                    bool within = false, bool last = false)
    {
        AuthoredValue result;
        const std::vector<AuthoredValue> arguments{ AuthoredValue::Entity(container), definition, tag,
                                                    AuthoredValue::Bool(within), AuthoredValue::Bool(last) };
        const AuthoredQueryStatus status =
            Queries->Evaluate(Queries->Registry().Resolve(AuthoredApiDefinition<InventoryOperations>::Name_Entry), arguments, result);
        if (status == AuthoredQueryStatus::Unavailable)
            return std::nullopt;
        EntityId entry;
        EXPECT_EQ(status, AuthoredQueryStatus::Value);
        EXPECT_TRUE(result.TryGetEntity(entry));
        return entry;
    }

    static AuthoredValue Item(std::string path)
    {
        return AuthoredValue::DataAsset(AssetRef{ .Type = AssetType::Data, .Path = std::move(path) });
    }

    FixedSimulationLoop Clock;
    InventoryOperations Operations{ Entities(), Clock };
    std::pair<VerbId, VerbAnswer> Last;
    std::unique_ptr<VerbDispatcher> Verbs;
    std::unique_ptr<AuthoredQueryDispatcher> Queries;
    std::vector<VerbBindingToken> Tokens;
    AuthoredQueryBindingToken QueryToken;
    AuthoredApiBindings Searches;
};
}

TEST_F(InventoryVocabularyTest, TheVerbsChangeWhatTheCountSays)
{
    const AuthoredValue arrows = Item("asset://items/arrow.sdata");
    EXPECT_EQ(Invoke(kInventoryGiveVerb, { { "into", Hero } }, { Definition("asset://items/arrow.sdata"), Count(25) }),
              VerbAdmission::Accepted);
    EXPECT_EQ(CountIn(Hero, arrows), 25);

    const EntityId chest = Container();
    EXPECT_EQ(Invoke(kInventoryTransferVerb, { { "entry", Held(Hero)[0] }, { "into", chest } }, { Count(4) }),
              VerbAdmission::Accepted);
    EXPECT_EQ(CountIn(Hero, arrows), 21);
    EXPECT_EQ(CountIn(chest, arrows), 4);

    EXPECT_EQ(Invoke(kInventoryDropVerb, { { "entry", Held(chest)[0] } }), VerbAdmission::Accepted);
    EXPECT_TRUE(Held(chest).empty());
    EXPECT_EQ(Invoke(kInventoryTakeVerb, { { "pickup", Pickup(Cloak, 1) }, { "into", chest } }), VerbAdmission::Accepted);
    EXPECT_EQ(Held(chest).size(), 1u);

    const EntityId wardrobe = Container({ .Capacity = 0, .Accepts = Wearable });
    EXPECT_EQ(Invoke(kInventoryGiveVerb, { { "into", wardrobe } }, { Definition("asset://items/arrow.sdata"), Count(1) }),
              VerbAdmission::Refused);
    EXPECT_EQ(LastAnswer(), "refused: not_accepted") << "the reason by name, not number";
}

TEST_F(InventoryVocabularyTest, ADropLandsInTheZoneWhereItFallsAndIsKeptThereUnlessToldNot)
{
    const StoragePartitionId room = Runtime.AttachZone(ZoneId{ 41 }).Partition;
    Entities().SetResource(ZonePlacement{ .PartitionAt = [room](const Vec3d& point) {
        return point.X > 0.0f ? std::optional<StoragePartitionId>(room) : std::nullopt;
    } });
    const auto drop = [&](float x, std::optional<bool> keep) {
        const EntityId entry = Inventory(Entities(), FixedSimTime{}).Give(Hero, Cloak, 1).Entry;
        std::vector<VerbBindingArgument> literals(1);
        literals[0].Key = "at";
        literals[0].Literal = *JsonParse("[" + std::to_string(x) + ", 0, 0]");
        if (keep.has_value())
        {
            literals.emplace_back();
            literals.back().Key = "keep";
            literals.back().Literal = JsonValue(*keep);
        }
        EXPECT_EQ(Invoke(kInventoryDropVerb, { { "entry", entry } }, std::move(literals)), VerbAdmission::Accepted);
        return Index().HolderOf(entry);
    };

    const EntityId kept = drop(3.0f, std::nullopt);
    EXPECT_EQ(Entities().GetEntityPartition(kept), room) << "not the holder's partition, the zone where it fell";
    EXPECT_TRUE(Entities().HasComponent<KeptWithZone>(kept)) << "kept unless told not";
    EXPECT_FALSE(IsAuthoredPersistentEntityId(IdOf(kept)));

    const EntityId passing = drop(3.0f, false);
    EXPECT_EQ(Entities().GetEntityPartition(passing), room);
    EXPECT_FALSE(Entities().HasComponent<KeptWithZone>(passing));

    const EntityId nowhere = drop(-3.0f, std::nullopt);
    EXPECT_EQ(Entities().GetEntityPartition(nowhere), Entities().GetEntityPartition(Hero))
        << "with no zone where it falls, it lands beside its holder";
}

TEST_F(InventoryVocabularyTest, TheCountTakesADefinitionOrATagAndCanLookInsideWhatIsHeld)
{
    Inventory inventory(Entities(), FixedSimTime{});
    (void)inventory.Give(Hero, Arrows, 3);
    (void)inventory.Give(Hero, Cloak, 1);
    const EntityId bag = BagIn(Hero);
    (void)inventory.Give(bag, Arrows, 5);
    (void)inventory.Give(bag, Cloak, 2);

    EXPECT_EQ(CountIn(Hero, Item("asset://items/arrow.sdata")), 3) << "what the bag holds is not the hero's own";
    EXPECT_EQ(CountIn(Hero, Item("asset://items/arrow.sdata"), {}, true), 8);
    EXPECT_EQ(CountIn(Hero, {}, AuthoredValue::Tag(Wearable)), 1);
    EXPECT_EQ(CountIn(Hero, {}, AuthoredValue::Tag(Wearable), true), 3);
    EXPECT_EQ(CountIn(Hero, {}), 5) << "no filter counts every unit, the bag's own included";
    EXPECT_EQ(CountIn(Hero, Item("asset://items/none.sdata")), 0);
}

TEST_F(InventoryVocabularyTest, TheEntrySearchAnswersTheFirstOrLastUnspentEntryInContainerOrder)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId first = inventory.Give(Hero, Cloak, 1).Entry;
    (void)inventory.Give(Hero, Arrows, 3);
    const EntityId second = inventory.Give(Hero, Cloak, 1).Entry;
    const AuthoredValue cloak = Item("asset://items/cloak.sdata");

    EXPECT_EQ(EntryIn(Hero, cloak), first);
    EXPECT_EQ(EntryIn(Hero, cloak, {}, false, true), second) << "the last in the container's order";
    SetItemCount(Entities(), first, 0);
    EXPECT_EQ(EntryIn(Hero, cloak), second) << "a spent entry is passed over";
    SetItemCount(Entities(), second, 0);
    EXPECT_EQ(EntryIn(Hero, cloak), std::nullopt);
}

TEST_F(InventoryVocabularyTest, TheEntrySearchLooksInsideWhatIsHeldOnlyWhenAsked)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId bag = BagIn(Hero);
    const EntityId packed = inventory.Give(bag, Cloak, 1).Entry;
    const AuthoredValue cloak = Item("asset://items/cloak.sdata");

    EXPECT_EQ(EntryIn(Hero, cloak), std::nullopt) << "what the bag holds is not the hero's own";
    EXPECT_EQ(EntryIn(Hero, cloak, {}, true), packed);
    const EntityId worn = inventory.Give(Hero, Cloak, 1).Entry;
    EXPECT_EQ(EntryIn(Hero, cloak, {}, true), packed) << "a nested entry's contents follow it, before what comes after";
    EXPECT_EQ(EntryIn(Hero, cloak, {}, true, true), worn);
}

TEST_F(InventoryVocabularyTest, TheEntrySearchMatchesATagOrOneBeneathItAndAnswersNothingWithoutAMatch)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId arrows = inventory.Give(Hero, Arrows, 3).Entry;
    const EntityId cloak = inventory.Give(Hero, Cloak, 1).Entry;
    const GameplayTagId item = Entities().GetResource<GameplayTagRegistry>().FindTag("Item");

    EXPECT_EQ(EntryIn(Hero, {}, AuthoredValue::Tag(Wearable)), cloak);
    EXPECT_EQ(EntryIn(Hero, {}, AuthoredValue::Tag(item)), cloak) << "a tag beneath the one asked for matches";
    EXPECT_EQ(EntryIn(Hero, {}), arrows) << "no filter answers the first entry";
    EXPECT_EQ(EntryIn(Hero, Item("asset://items/bag.sdata")), std::nullopt);
    EXPECT_EQ(EntryIn(Hero, Item("asset://items/none.sdata")), std::nullopt) << "an item content never declared";
    EXPECT_EQ(EntryIn(Container(), {}), std::nullopt) << "an empty container";
}

namespace
{
class InventoryPropertyTest : public InventoryVocabularyTest
{
protected:
    InventoryPropertyTest()
    {
        GameplayTagRegistry& tags = Entities().GetResource<GameplayTagRegistry>();
        Weight = *tags.RegisterTag("Item.Weight");
        ItemRegistry& items = Entities().GetResource<ItemRegistry>();
        Stone = items.Define("asset://items/stone.sdata",
                             ItemDefinition{ .Name = "Stone", .StackMax = 10,
                                             .Properties = { { Weight, AuthoredValue::Float(2.5) } } });
        Brick = items.Define("asset://items/brick.sdata",
                             ItemDefinition{ .Name = "Brick", .Tags = { Wearable },
                                             .Properties = { { Weight, AuthoredValue::Float(4.0) } } });
        Letter = items.Define("asset://items/letter.sdata",
                              ItemDefinition{ .Name = "Letter",
                                              .Properties = { { Weight, AuthoredValue::String("light") } } });
    }

    std::optional<double> Ask(std::string_view query, std::vector<AuthoredValue> arguments)
    {
        AuthoredValue result;
        const AuthoredQueryStatus status = Queries->Evaluate(Queries->Registry().Resolve(query), arguments, result);
        if (status == AuthoredQueryStatus::Unavailable)
            return std::nullopt;
        double number = -1.0;
        EXPECT_EQ(status, AuthoredQueryStatus::Value);
        EXPECT_TRUE(result.TryGetNumber(number));
        return number;
    }

    std::optional<double> PropertyOf(EntityId entry)
    {
        return Ask(AuthoredApiDefinition<InventoryOperations>::Name_Property,
                   { AuthoredValue::Entity(entry), AuthoredValue::Tag(Weight) });
    }

    std::optional<double> TotalIn(EntityId container, AuthoredValue definition = {}, AuthoredValue tag = {},
                                  bool within = false)
    {
        return Ask(AuthoredApiDefinition<InventoryOperations>::Name_Total,
                   { AuthoredValue::Entity(container), AuthoredValue::Tag(Weight), definition, tag,
                     AuthoredValue::Bool(within) });
    }

    GameplayTagId Weight;
    ItemId Stone;
    ItemId Brick;
    ItemId Letter;
};
}

TEST_F(InventoryPropertyTest, AnEntrysPropertyIsTheNumberItsItemStates)
{
    Inventory inventory(Entities(), FixedSimTime{});
    EXPECT_EQ(PropertyOf(inventory.Give(Hero, Stone, 7).Entry), std::optional<double>(2.5)) << "of a stack, per unit";
    EXPECT_EQ(PropertyOf(inventory.Give(Hero, Arrows, 3).Entry), std::nullopt) << "an item stating no weight";
    EXPECT_EQ(PropertyOf(inventory.Give(Hero, Letter, 1).Entry), std::nullopt) << "a weight that is not a number";
    EXPECT_EQ(PropertyOf(Hero), std::nullopt) << "not an entry";
}

TEST_F(InventoryPropertyTest, ATotalSumsEachMatchingEntrysCountTimesItsNumber)
{
    Inventory inventory(Entities(), FixedSimTime{});
    (void)inventory.Give(Hero, Stone, 12);
    (void)inventory.Give(Hero, Brick, 1);
    const EntityId spent = inventory.Give(Hero, Brick, 1).Entry;
    (void)inventory.Give(Hero, Letter, 1);
    (void)inventory.Give(Hero, Arrows, 5);
    const EntityId bag = BagIn(Hero);
    (void)inventory.Give(bag, Stone, 4);
    SetItemCount(Entities(), spent, 0);

    EXPECT_EQ(TotalIn(Hero), std::optional<double>(12 * 2.5 + 4.0)) << "two stacks of stone and one brick";
    EXPECT_EQ(TotalIn(Hero, {}, {}, true), std::optional<double>(16 * 2.5 + 4.0)) << "with what the bag holds";
    EXPECT_EQ(TotalIn(Hero, Item("asset://items/stone.sdata"), {}, true), std::optional<double>(16 * 2.5));
    EXPECT_EQ(TotalIn(Hero, {}, AuthoredValue::Tag(Wearable)), std::optional<double>(4.0));
    EXPECT_EQ(TotalIn(Hero, Item("asset://items/none.sdata")), std::optional<double>(0.0));
    EXPECT_EQ(TotalIn(Container()), std::optional<double>(0.0));
    EXPECT_EQ(ItemPropertyTotal(Entities(), Hero, Weight, ItemFilter{ .Within = true }), 16 * 2.5 + 4.0);
}

TEST_F(InventoryConsumingTest, TheGatePicksUpStacksSplitsMovesDropsConsumesAndReaps)
{
    Inventory inventory(Entities(), FixedSimTime{});
    ASSERT_EQ(inventory.Take(Pickup(Arrows, 12), Hero).Refusal, InventoryRefusal::None);
    ASSERT_EQ(inventory.Take(Pickup(Arrows, 15), Hero).Refusal, InventoryRefusal::None);
    std::vector<EntityId> quiver = Held(Hero);
    ASSERT_EQ(quiver.size(), 2u) << "picked up, stacked to twenty, and the rest a second stack";
    EXPECT_EQ(CountOf(quiver[0]), 20u);
    EXPECT_EQ(CountOf(quiver[1]), 7u);

    const EntityId chest = Container();
    const InventoryResult split = inventory.Transfer(quiver[0], chest, 5);
    ASSERT_EQ(split.Refusal, InventoryRefusal::None);
    EXPECT_EQ(CountOf(quiver[0]), 15u);
    EXPECT_EQ(CountOf(split.Entry), 5u);

    const InventoryResult dropped = inventory.Drop(split.Entry, Transform3f::Identity(), StoragePartitionId{ 0 });
    ASSERT_EQ(dropped.Refusal, InventoryRefusal::None);
    EXPECT_TRUE(Held(chest).empty());
    ASSERT_EQ(inventory.Take(dropped.Entry, Hero).Refusal, InventoryRefusal::None);
    EXPECT_EQ(Held(Hero).size(), 2u) << "the dropped stack came back into the stack it left";
    EXPECT_EQ(CountOf(quiver[0]), 20u);

    const EntityId potion = inventory.Give(Hero, Potion, 1).Entry;
    ASSERT_EQ(inventory.Use(Hero, potion).Refusal, InventoryRefusal::None);
    Step();
    EXPECT_EQ(CountOf(potion), 0u);
    EXPECT_TRUE(Entities().IsAlive(potion)) << "depleted while its activation runs";
    for (int i = 0; i < 6; ++i)
        Step();
    EXPECT_FALSE(Entities().IsAlive(potion)) << "reaped once it ended";
    EXPECT_EQ(Held(Hero).size(), 2u);
}

TEST_F(InventoryTest, WhatAnEntryHoldsGoesWhereTheEntryGoes)
{
    const Cellar cellar;
    Import(cellar.Zone, cellar.Package());
    const EntityId chest = Resolve(Cellar::kChest);
    ASSERT_TRUE(chest.IsValid());

    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId bottle = BagIn(chest);
    const EntityId contents = inventory.Give(bottle, Cloak, 1).Entry;
    ASSERT_EQ(Entities().GetEntityPartition(contents), Entities().GetEntityPartition(chest));

    ASSERT_EQ(inventory.Transfer(bottle, Hero, 0).Refusal, InventoryRefusal::None);
    FinishResidency();
    EXPECT_EQ(Entities().GetEntityPartition(contents), Entities().GetEntityPartition(Hero));
    Detach(cellar.Zone);
    EXPECT_TRUE(Entities().IsAlive(contents));
    EXPECT_EQ(Held(bottle), std::vector{ contents });
}

TEST_F(InventoryTest, AMoveFillsOpenStacksBeforeItTakesRoom)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId chest = Container({ .Capacity = 1, .Accepts = {} });
    ASSERT_EQ(inventory.Give(chest, Arrows, 15).Refusal, InventoryRefusal::None);
    const EntityId bundle = inventory.Give(Hero, Arrows, 5).Entry;

    ASSERT_EQ(inventory.Transfer(bundle, chest, 0).Refusal, InventoryRefusal::None) << "the chest's stack had room";
    ASSERT_EQ(Held(chest).size(), 1u);
    EXPECT_EQ(CountOf(Held(chest)[0]), 20u);
    EXPECT_EQ(CountOf(bundle), 0u) << "merged away, it is spent";
    RunInventoryPass(Entities());
    EXPECT_FALSE(Entities().IsAlive(bundle));

    const EntityId loose = inventory.Give(Hero, Arrows, 4).Entry;
    const EntityId pickup = inventory.Drop(loose, Transform3f::Identity(), StoragePartitionId{ 0 }).Entry;
    ASSERT_EQ(inventory.Give(Hero, Arrows, 6).Refusal, InventoryRefusal::None);
    ASSERT_EQ(inventory.Take(pickup, Hero).Refusal, InventoryRefusal::None);
    ASSERT_EQ(Held(Hero).size(), 1u) << "a dropped stack taken back joins the stack already held";
    EXPECT_EQ(CountOf(Held(Hero)[0]), 10u);
}

TEST_F(InventoryTest, AWholeEntryMovedIntoTheFullContainerHoldingItChangesNothing)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId wardrobe = Container({ .Capacity = 2, .Accepts = Wearable });
    ASSERT_EQ(inventory.Give(wardrobe, Cloak, 2).Refusal, InventoryRefusal::None);
    const std::vector<EntityId> before = Held(wardrobe);
    ASSERT_EQ(before.size(), 2u);
    const std::uint16_t position = Entities().TryGet<::Held>(before[0])->Position;
    const PersistentEntityId identity = IdOf(before[0]);

    const InventoryResult moved = inventory.Transfer(before[0], wardrobe, 0);
    ASSERT_EQ(moved.Refusal, InventoryRefusal::None) << "it takes no room it does not already take";
    EXPECT_EQ(moved.Entry, before[0]);
    EXPECT_EQ(Held(wardrobe), before);
    EXPECT_EQ(Entities().TryGet<::Held>(before[0])->Position, position) << "its place in the container is kept";
    EXPECT_EQ(IdOf(before[0]), identity);
    EXPECT_EQ(CountOf(before[0]), 1u);
}

TEST_F(InventoryTest, SplittingAStackInsideItsFullContainerIsRefused)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId chest = Container({ .Capacity = 1, .Accepts = {} });
    ASSERT_EQ(inventory.Give(chest, Arrows, 20).Refusal, InventoryRefusal::None);
    const EntityId stack = Held(chest)[0];

    EXPECT_EQ(inventory.Transfer(stack, chest, 5).Refusal, InventoryRefusal::ContainerFull)
        << "part of a stack is a new entry, and the container has no room for one";
    EXPECT_EQ(Held(chest), std::vector{ stack });
    EXPECT_EQ(CountOf(stack), 20u);
}

TEST_F(InventoryTest, AnEntryCannotGoInsideItself)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId bag = BagIn(Hero);
    const EntityId pouch = BagIn(bag);

    EXPECT_EQ(inventory.Transfer(bag, bag, 0).Refusal, InventoryRefusal::NotAccepted);
    EXPECT_EQ(inventory.Transfer(bag, pouch, 0).Refusal, InventoryRefusal::NotAccepted);
    const EntityId dropped = inventory.Drop(bag, Transform3f::Identity(), StoragePartitionId{ 0 }).Entry;
    EXPECT_EQ(inventory.Take(dropped, pouch).Refusal, InventoryRefusal::NotAccepted);
    EXPECT_EQ(Held(bag), std::vector{ pouch });
    EXPECT_EQ(inventory.Take(dropped, Hero).Refusal, InventoryRefusal::None);
}

TEST_F(InventoryTest, ADroppedEntryMovesOnlyByTakingItsPickup)
{
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId cloak = inventory.Give(Hero, Cloak, 1).Entry;
    const EntityId pickup = inventory.Drop(cloak, Transform3f::Identity(), StoragePartitionId{ 0 }).Entry;
    ASSERT_TRUE(pickup.IsValid());
    EXPECT_EQ(inventory.Drop(cloak, Transform3f::Identity(), StoragePartitionId{ 0 }).Refusal,
              InventoryRefusal::Missing);
    EXPECT_EQ(inventory.Transfer(cloak, Hero, 0).Refusal, InventoryRefusal::Missing);
    EXPECT_EQ(inventory.Take(pickup, Hero).Entry, cloak);
}

TEST_F(InventoryTest, APrefabsEntryHoldsToItsOwnSpawnsContainerWhenTheSpawnStripsIdentity)
{
    ComponentSerializerRegistry serializers;
    ComponentRegistrar registrar(nullptr, &serializers, nullptr);
    RegisterEngineComponents(registrar);
    LoggingProvider logging;
    SceneSerializationContext context{ logging };

    const auto identity = [](std::string_view hex) {
        return std::pair{ ResolveComponentTypeId<PersistentIdComponent>(),
                          JsonValue(JsonValue::Object{ { "id", JsonValue(std::string(hex)) } }) };
    };
    SmapContents guard;
    SmapEntityRecord holder;
    holder.Persistent = PersistentEntityId{ 0x6A4D };
    holder.Components = { identity("0000000000006a4d"),
                          { ResolveComponentTypeId<ItemContainer>(), JsonValue(JsonValue::Object{}) } };
    SmapEntityRecord purse;
    purse.Persistent = PersistentEntityId{ 0x9E5E };
    purse.Components = {
        identity("0000000000009e5e"),
        { ResolveComponentTypeId<ItemEntry>(),
          JsonValue(JsonValue::Object{ { "definition", JsonValue(std::string("asset://items/arrow.sdata")) },
                                       { "count", JsonValue(12.0) } }) },
        { ResolveComponentTypeId<::Held>(),
          JsonValue(JsonValue::Object{ { "holder", JsonValue(std::string("0000000000006a4d")) },
                                       { "position", JsonValue(0.0) } }) }
    };
    guard.Entities = { purse, holder };
    EntityBuildPackage package;
    ASSERT_TRUE(BuildEntityPackageFromSmap(guard, serializers, package, SmapPackageOptions{ .StripAuthoredIdentity = true }));

    std::vector<std::vector<EntityId>> spawns(2);
    for (std::vector<EntityId>& spawned : spawns)
    {
        ZoneImportError error;
        ASSERT_TRUE(ImportPackageIntoPartition(Entities(), Schema, package, StoragePartitionId::Default(), ZoneStateScope{},
                                               serializers, context, &error, &spawned))
            << error.Message;
        ASSERT_EQ(spawned.size(), 2u);
    }
    SettleHeldRecords(Entities());
    for (const std::vector<EntityId>& spawned : spawns)
    {
        EXPECT_EQ(Held(spawned[1]), std::vector{ spawned[0] });
        const PersistentIdComponent* guardId = Entities().TryGet<PersistentIdComponent>(spawned[1]);
        ASSERT_NE(guardId, nullptr) << "settled, the guard has an identity, so the reference saves";
        EXPECT_EQ(Entities().TryGet<::Held>(spawned[0])->Holder.Target, guardId->Id);
        EXPECT_NE(guardId->Id, PersistentEntityId{ 0x6A4D });
    }
}

TEST_F(InventoryTest, AnAuthoredEntryTakenFromItsZoneTakesARuntimeIdAndIsNotMadeAgain)
{
    Cellar cellar;
    cellar.Sword = Cloak;
    Import(cellar.Zone, cellar.Package());
    const EntityId sword = Resolve(Cellar::kSword);
    ASSERT_TRUE(sword.IsValid());
    ASSERT_EQ(Held(Resolve(Cellar::kChest)), std::vector{ sword });

    ASSERT_EQ(Inventory(Entities(), FixedSimTime{}).Transfer(sword, Hero, 0).Refusal, InventoryRefusal::None);
    EXPECT_FALSE(IsAuthoredPersistentEntityId(IdOf(sword)));
    EXPECT_FALSE(Resolve(Cellar::kSword).IsValid());

    Detach(cellar.Zone);
    EXPECT_TRUE(Entities().IsAlive(sword));
    EXPECT_TRUE(Entities().GetResource<ZoneStateStore>().IsRecordedDestroyed(ScopeOf(cellar.Zone), Cellar::kSword));
    Import(cellar.Zone, cellar.Package());
    EXPECT_FALSE(Resolve(Cellar::kSword).IsValid()) << "no second sword";
    EXPECT_TRUE(Held(Resolve(Cellar::kChest)).empty());
    EXPECT_EQ(Held(Hero), std::vector{ sword });
}

TEST_F(InventoryTest, AZoneThatForgetsMakesItsEntryAgainBesideTheTakenOne)
{
    Cellar cellar;
    cellar.Sword = Cloak;
    Import(cellar.Zone, cellar.Package());
    const EntityId taken = Resolve(Cellar::kSword);
    ASSERT_EQ(Inventory(Entities(), FixedSimTime{}).Transfer(taken, Hero, 0).Refusal, InventoryRefusal::None);
    Detach(cellar.Zone);
    Entities().GetResource<ZoneStateStore>().Clear();

    Import(cellar.Zone, cellar.Package());

    const EntityId again = Resolve(Cellar::kSword);
    ASSERT_TRUE(again.IsValid());
    EXPECT_NE(again, taken);
    EXPECT_EQ(Entities().GetResource<PersistentEntityIndex>().CollisionCount(), 0u);
    EXPECT_EQ(Held(Hero), std::vector{ taken });
}

TEST_F(InventoryTest, SplittingAnAuthoredStackKeepsTheRemainderAcrossItsZonesRoundTrip)
{
    Cellar cellar;
    cellar.Sword = Arrows;
    cellar.Count = 12;
    Import(cellar.Zone, cellar.Package());
    const EntityId stack = Resolve(Cellar::kSword);
    ASSERT_EQ(Inventory(Entities(), FixedSimTime{}).Transfer(stack, Hero, 5).Refusal, InventoryRefusal::None);
    EXPECT_FALSE(IsAuthoredPersistentEntityId(IdOf(stack)));

    Detach(cellar.Zone);
    Import(cellar.Zone, cellar.Package());

    const std::vector<EntityId> left = Held(Resolve(Cellar::kChest));
    ASSERT_EQ(left.size(), 1u) << "the authored stack is not made again beside the remainder";
    EXPECT_EQ(CountOf(left[0]), 7u);
}

TEST_F(InventoryTest, WhatADepositIntoAnAuthoredChestHoldsSurvivesItsZonesRoundTrip)
{
    const Cellar cellar;
    Import(cellar.Zone, cellar.Package());
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId bag = BagIn(Resolve(Cellar::kChest));
    const EntityId arrows = inventory.Give(bag, Arrows, 9).Entry;
    const PersistentEntityId bagId = IdOf(bag);

    Detach(cellar.Zone);
    ASSERT_TRUE(Entities().IsAlive(bag));
    EXPECT_EQ(Index().AwaitedHolderOf(bag), Cellar::kChest);
    EXPECT_EQ(Entities().GetEntityPartition(arrows), PersistentStoragePartition);
    EXPECT_FALSE(Entities().TryGet<NetRelevanceAnchor>(bag)->Anchor.IsValid()) << "it reaches nobody while it waits";

    Import(cellar.Zone, cellar.Package());
    const EntityId chest = Resolve(Cellar::kChest);
    EXPECT_EQ(Held(chest), std::vector{ bag });
    EXPECT_EQ(IdOf(bag), bagId);
    EXPECT_EQ(Held(bag), std::vector{ arrows });
    EXPECT_EQ(Entities().GetEntityPartition(arrows), Entities().GetEntityPartition(chest));
    EXPECT_EQ(Entities().TryGet<NetRelevanceAnchor>(bag)->Anchor, chest);
}

TEST_F(InventoryTest, AChestForgottenAtDetachKeepsNothingPutInIt)
{
    Cellar cellar;
    EntityBuildPackage package = cellar.Package();
    ASSERT_TRUE(package.AddComponent<ZoneStateLifetime>(PackageEntityId{ 0 },
                                                        ZoneStateLifetime{ .Forgotten = ZoneStateForgetting::AtDetach }));
    Import(cellar.Zone, package);
    const EntityId deposit = Inventory(Entities(), FixedSimTime{}).Give(Resolve(Cellar::kChest), Arrows, 3).Entry;

    Detach(cellar.Zone);

    EXPECT_FALSE(Entities().IsAlive(deposit));
}

TEST_F(InventoryTest, WhatADepositIntoAnAuthoredChestHoldsSurvivesASaveIntoAFreshWorld)
{
    ComponentSerializerRegistry serializers;
    ComponentRegistrar registrar(nullptr, &serializers, nullptr);
    RegisterEngineComponents(registrar);
    LoggingProvider logging;
    SceneSerializationContext context{ logging };

    const Cellar cellar;
    Import(cellar.Zone, cellar.Package());
    Inventory inventory(Entities(), FixedSimTime{});
    const EntityId bag = BagIn(Resolve(Cellar::kChest));
    (void)inventory.Give(bag, Arrows, 9);
    const PersistentEntityId bagId = IdOf(bag);
    const std::vector<ScopedHeldRecords> saved = CaptureLevelHeldRecords(
        Entities(), [&](EntityId) { return ScopeOf(cellar.Zone); }, serializers, context);
    ASSERT_EQ(saved.size(), 1u);

    InventoryWorld fresh;
    fresh.Entities().GetResource<ZoneStateStore>().RecordHeld(fresh.ScopeOf(cellar.Zone), saved[0].Records, {});
    ZoneImportError error;
    ASSERT_TRUE(ImportZonePackage(fresh.Runtime, fresh.Schema, cellar.Zone, cellar.Package(), serializers, context, kLogic,
                                  &error, fresh.ScopeOf(cellar.Zone)))
        << error.Message;
    fresh.FinishResidency();

    const EntityId restored = fresh.Resolve(bagId);
    ASSERT_TRUE(restored.IsValid());
    EXPECT_EQ(fresh.Held(fresh.Resolve(Cellar::kChest)), std::vector{ restored });
    ASSERT_EQ(fresh.Held(restored).size(), 1u);
    EXPECT_EQ(fresh.CountOf(fresh.Held(restored)[0]), 9u);
    EXPECT_TRUE(fresh.Entities().HasComponent<ItemContainer>(restored)) << "the bag still holds things";
}

TEST_F(InventoryTest, AnEquipEffectMustBeMaintainedAndItsParametersNameItsGrants)
{
    EffectDefinition lasting{};
    lasting.Duration = EffectDuration::Infinite;
    EffectDefinition brief{};
    brief.Duration = EffectDuration::Duration;
    brief.DurationSeconds = 2.0f;
    EffectRegistry& effects = Entities().GetResource<EffectRegistry>();
    const EffectId aura = effects.Define("asset://effects/aura.sdata", lasting);
    (void)effects.Define("asset://effects/burst.sdata", brief);
    (void)Entities().GetResource<AbilityRegistry>().Reserve("asset://abilities/bolt.sdata");
    (void)DeclareEffectGrants(Entities(), aura,
                              { AbilityGrantDeclaration{ .Key = "g_bolt", .Ability = "asset://abilities/bolt.sdata",
                                                         .Override = {} } });

    ItemDeclaration ring;
    ring.Name = "Ring";
    ring.Occupies = { "ring" };
    ring.EquipEffects = { { .Effect = "asset://effects/aura.sdata", .Scale = 1.0f } };
    ring.GrantParameters = { { .Key = "g_bolt", .Parameters = { { .Key = "power", .Value = 25.0 } } } };
    ItemDefinition definition;
    std::vector<std::string> errors;
    ASSERT_TRUE(ResolveItemDeclaration(ring, Entities(), definition, errors)) << errors.front();
    ASSERT_EQ(definition.EquipEffects.size(), 1u);
    EXPECT_EQ(definition.EquipEffects[0].Effect, aura);
    EXPECT_EQ(definition.Occupies, std::vector<std::string>{ "ring" });

    ring.EquipEffects.push_back({ .Effect = "asset://effects/burst.sdata", .Scale = 1.0f });
    ring.GrantParameters.push_back({ .Key = "g_missing", .Parameters = {} });
    errors.clear();
    EXPECT_FALSE(ResolveItemDeclaration(ring, Entities(), definition, errors));
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_NE(errors[0].find("equip_effect_not_maintained"), std::string::npos) << errors[0];
    EXPECT_NE(errors[1].find("g_missing"), std::string::npos) << errors[1];
}

TEST_F(InventoryTest, AnItemStatesPropertiesOfEveryKindAndListsTheAssetsTheyName)
{
    GameplayTagRegistry& tags = Entities().GetResource<GameplayTagRegistry>();
    for (const char* name : { "Item.Weight", "Item.Description", "Item.Icon", "Item.Rarity", "Rarity.Uncommon",
                              "Item.Recipe" })
        (void)tags.RegisterTag(name);
    const DataAssetCompileResult compiled = CompileItemData(R"({ "name": "Arrow", "stack": { "max": 20 },
        "properties": [ { "key": "weight", "property": "Item.Weight", "value": { "number": 0.25 } },
                        { "key": "text", "property": "Item.Description", "value": { "text": "Fletched." } },
                        { "key": "icon", "property": "Item.Icon", "value": { "asset": "asset://ui/icons/arrow.stex" } },
                        { "key": "rarity", "property": "Item.Rarity", "value": { "tag": "Rarity.Uncommon" } },
                        { "key": "recipe", "property": "Item.Recipe", "value": { "data": "asset://recipes/arrow.sdata" } } ] })");
    ASSERT_TRUE(compiled.IsValid()) << compiled.Error;
    EXPECT_NE(std::ranges::find(compiled.Dependencies, std::string("asset://ui/icons/arrow.stex"), &AssetRef::Path),
              compiled.Dependencies.end());
    EXPECT_NE(std::ranges::find(compiled.Dependencies, std::string("asset://recipes/arrow.sdata"), &AssetRef::Path),
              compiled.Dependencies.end());

    ItemDefinition arrow;
    std::vector<std::string> errors;
    ASSERT_TRUE(ResolveItemDeclaration(*std::static_pointer_cast<const ItemDeclaration>(compiled.Value), Entities(),
                                       arrow, errors))
        << errors.front();
    EXPECT_EQ(ItemNumberOf(arrow, tags.FindTag("Item.Weight")), std::optional<double>(0.25));
    std::string_view text;
    ASSERT_NE(ItemPropertyOf(arrow, tags.FindTag("Item.Description")), nullptr);
    EXPECT_TRUE(ItemPropertyOf(arrow, tags.FindTag("Item.Description"))->TryGetString(text));
    EXPECT_EQ(text, "Fletched.");
    const AssetRef* icon = nullptr;
    ASSERT_NE(ItemPropertyOf(arrow, tags.FindTag("Item.Icon")), nullptr);
    EXPECT_TRUE(ItemPropertyOf(arrow, tags.FindTag("Item.Icon"))->TryGetAsset(icon));
    EXPECT_EQ(icon->Path, "asset://ui/icons/arrow.stex");
    GameplayTagId rarity;
    ASSERT_NE(ItemPropertyOf(arrow, tags.FindTag("Item.Rarity")), nullptr);
    EXPECT_TRUE(ItemPropertyOf(arrow, tags.FindTag("Item.Rarity"))->TryGetTag(rarity));
    EXPECT_EQ(rarity, tags.FindTag("Rarity.Uncommon"));
    EXPECT_EQ(ItemNumberOf(arrow, tags.FindTag("Item.Description")), std::nullopt) << "text is not a number";
    EXPECT_EQ(ItemPropertyOf(arrow, Wearable), nullptr) << "a property the item does not state";
}

TEST_F(InventoryTest, AnItemStatingAPropertyTwiceOrNamingAnUnknownTagIsRefused)
{
    (void)Entities().GetResource<GameplayTagRegistry>().RegisterTag("Item.Weight");
    const auto refusals = [&](std::string_view json) {
        const DataAssetCompileResult compiled = CompileItemData(json);
        EXPECT_TRUE(compiled.IsValid()) << compiled.Error;
        ItemDefinition definition;
        std::vector<std::string> errors;
        EXPECT_FALSE(ResolveItemDeclaration(*std::static_pointer_cast<const ItemDeclaration>(compiled.Value),
                                            Entities(), definition, errors));
        return errors;
    };
    const std::vector<std::string> twice = refusals(R"({ "properties": [
        { "key": "a", "property": "Item.Weight", "value": { "number": 1 } },
        { "key": "b", "property": "Item.Weight", "value": { "number": 2 } } ] })");
    ASSERT_EQ(twice.size(), 1u);
    EXPECT_NE(twice[0].find("'Item.Weight' is stated twice"), std::string::npos) << twice[0];
    const std::vector<std::string> unknown = refusals(R"({ "properties": [
        { "key": "a", "property": "Item.Heft", "value": { "number": 1 } },
        { "key": "b", "property": "Item.Weight", "value": { "tag": "Rarity.Mythic" } } ] })");
    ASSERT_EQ(unknown.size(), 2u);
    EXPECT_NE(unknown[0].find("Item.Heft"), std::string::npos) << unknown[0];
    EXPECT_NE(unknown[1].find("Rarity.Mythic"), std::string::npos) << unknown[1];
    EXPECT_NE(CompileItemData(R"({ "properties": [ { "key": "a", "property": "Item.Weight",
                                  "value": { "number": 1, "text": "one" } } ] })").Error.find("exactly one"),
              std::string::npos);
}
