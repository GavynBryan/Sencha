// Equipment (docs/gameplay/equipment.md): layouts, and the records a wearer
// holds against them.

#include <abilities/AbilityActivations.h>
#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySystem.h>
#include <ecs/WorldComponentSchema.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <equipment/Equipment.h>
#include <equipment/EquipmentSystem.h>
#include <equipment/WornGrantEntries.h>
#include <equipment/EquipmentVocabulary.h>
#include <app/EngineVocabulary.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <time/SimClock.h>
#include <inventory/InventorySystem.h>
#include <abilities/AbilityRequestQueue.h>
#include <abilities/AbilityTrace.h>
#include <abilities/ActiveAbilities.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <effects/EffectDefinition.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagSerialization.h>
#include <time/ActorTimeSystem.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryKit.h>
#include <inventory/InventoryComponents.h>
#include <inventory/ItemDefinitionData.h>
#include <world/SimulationAuthority.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <runtime/spawn/SceneSpawnOverlay.h>
#include <save/HeldRecordSaves.h>
#include <world/build/EntityBuildPackage.h>
#include <zone/ZonePackageImporter.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/World.h>
#include <equipment/EquipmentKit.h>
#include <equipment/EquipmentLayoutData.h>
#include <equipment/EquipmentSerializers.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/ComponentRegistrar.h>
#include <world/registry/Registry.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
    // Compiles `json` as an equipment.layout's data.
    std::shared_ptr<const EquipmentLayoutDeclaration> Compile(std::string_view json)
    {
        DataAssetTypeRegistry types;
        DataSchemaRegistry schemas;
        RegisterEquipmentLayoutData(types, schemas);
        const std::optional<JsonValue> data = JsonParse(json);
        EXPECT_TRUE(data.has_value());
        const DataAssetCompileResult compiled = types.Find(kEquipmentLayoutType)->Compile(*data);
        EXPECT_TRUE(compiled.Error.empty()) << compiled.Error;
        return std::static_pointer_cast<const EquipmentLayoutDeclaration>(compiled.Value);
    }

    constexpr std::string_view kHumanoid = R"({
        "slots": [ { "key": "hand.main", "accepts": "Item.Weapon", "action": "Input.Fire", "priority": 10 },
                   { "key": "ring.left", "accepts": "Item.Ring", "priority": 20, "order": 2 },
                   { "key": "ring.right", "accepts": "Item.Ring", "priority": 21, "order": 3,
                     "on_depleted": "hold" } ],
        "lock_tags": [ "State.Attacking" ] })";
}

TEST(EquipmentLayout, ALayoutResolvesItsTagsAndBindings)
{
    World world;
    GameplayTagRegistry& tags = world.AddResource<GameplayTagRegistry>();
    const GameplayTagId weapon = *tags.RegisterTag("Item.Weapon");
    const GameplayTagId ring = *tags.RegisterTag("Item.Ring");
    const GameplayTagId attacking = *tags.RegisterTag("State.Attacking");

    EquipmentLayout layout;
    std::vector<std::string> errors;
    ASSERT_TRUE(ResolveEquipmentLayoutDeclaration(*Compile(kHumanoid), world, layout, errors)) << errors.front();
    ASSERT_EQ(layout.Slots.size(), 3u);
    EXPECT_EQ(layout.Slots[0].Accepts, weapon);
    EXPECT_EQ(layout.Slots[0].Binding.Action, std::optional<std::string>("Input.Fire"));
    EXPECT_EQ(layout.Slots[0].Binding.Priority, std::optional<std::int32_t>(10));
    EXPECT_EQ(layout.Slots[2].Accepts, ring);
    EXPECT_EQ(layout.Slots[2].OnDepleted, SlotDepletion::Hold);
    EXPECT_EQ(layout.Slots[1].OnDepleted, SlotDepletion::Release);
    EXPECT_EQ(layout.LockTags, std::vector<GameplayTagId>{ attacking });
}

TEST(EquipmentLayout, AnUnknownTagIsReportedByName)
{
    World world;
    (void)world.AddResource<GameplayTagRegistry>().RegisterTag("Item.Weapon");
    EquipmentLayout layout;
    std::vector<std::string> errors;
    EXPECT_FALSE(ResolveEquipmentLayoutDeclaration(*Compile(kHumanoid), world, layout, errors));
    EXPECT_EQ(errors, (std::vector<std::string>{ "no gameplay tag 'Item.Ring'", "no gameplay tag 'Item.Ring'",
                                                 "no gameplay tag 'State.Attacking'" }));
}

TEST(EquipmentLayout, AMalformedLayoutIsRefusedWithWhere)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    RegisterEquipmentLayoutData(types, schemas);
    const auto refusal = [&](std::string_view json) {
        return types.Find(kEquipmentLayoutType)->Compile(*JsonParse(json)).Error;
    };
    EXPECT_NE(refusal(R"({ "slots": [] })").find("at least one slot"), std::string::npos);
    EXPECT_NE(refusal(R"({ "slots": [ { "key": "ring." } ] })").find("$.data.slots[0].key"), std::string::npos);
    EXPECT_NE(refusal(R"({ "slots": [ { "key": "a" }, { "key": "a" } ] })").find("names two slots"), std::string::npos);
    EXPECT_NE(refusal(R"({ "slots": [ { "key": "a", "on_depleted": "keep" } ] })").find("on_depleted"),
              std::string::npos);
    EXPECT_NE(refusal(R"({ "slots": [ { "key": "a..b" } ] })").find("$.data.slots[0].key"), std::string::npos);
    const std::string both = refusal(R"({ "slots": [ { "key": "ring.left" }, { "key": "ring" } ] })");
    EXPECT_NE(both.find("$.data.slots[1].key"), std::string::npos) << both;
    EXPECT_NE(both.find("'ring' is a slot and the group of 'ring.left'"), std::string::npos)
        << "an item naming ring would reach the slot and never the group";
}

TEST(EquipmentLayout, ALayoutHoldsThirtyTwoSlotsAndRefusesOneMore)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    RegisterEquipmentLayoutData(types, schemas);
    const auto refusal = [&](std::size_t slots) {
        std::string json = R"({ "slots": [)";
        for (std::size_t i = 0; i < slots; ++i)
            json += std::string(i == 0 ? "" : ",") + R"({ "key": "s)" + std::to_string(i) + R"(" })";
        return types.Find(kEquipmentLayoutType)->Compile(*JsonParse(json + "] }")).Error;
    };
    EXPECT_EQ(refusal(kMaxEquipmentSlots), "");
    EXPECT_NE(refusal(kMaxEquipmentSlots + 1).find("at most 32 slots"), std::string::npos);
}

TEST(EquipmentLayout, AGroupNamesItsSlotsInLayoutOrder)
{
    World world;
    GameplayTagRegistry& tags = world.AddResource<GameplayTagRegistry>();
    for (const char* name : { "Item.Weapon", "Item.Ring", "State.Attacking" })
        (void)tags.RegisterTag(name);
    EquipmentLayout layout;
    std::vector<std::string> errors;
    ASSERT_TRUE(ResolveEquipmentLayoutDeclaration(*Compile(kHumanoid), world, layout, errors));

    EXPECT_EQ(EquipmentSlotsNamed(layout, "ring"), (std::vector<std::uint8_t>{ 1, 2 }));
    EXPECT_EQ(EquipmentSlotsNamed(layout, "ring.right"), std::vector<std::uint8_t>{ 2 });
    EXPECT_TRUE(EquipmentSlotsNamed(layout, "rin").empty()) << "a group ends at a '.'";
    EXPECT_TRUE(EquipmentSlotsNamed(layout, "").empty());
    EXPECT_EQ(FindEquipmentSlot(layout, "hand.main"), 0u);
    EXPECT_EQ(FindEquipmentSlot(layout, "hand"), kNoEquipmentSlot);
}

namespace
{
    // A World that loads scenes naming layouts and items: registries, and the
    // humanoid layout defined from kHumanoid.
    struct SceneWorld
    {
        LoggingProvider Logging;
        SceneSerializationContext Context{ Logging };
        Registry Scene;
        EquipmentLayoutId Humanoid;

        explicit SceneWorld(std::string_view firstItem = {})
        {
            ComponentRegistrar(Scene.Components).AddAll<EquipmentComponents>();
            Scene.Components.RegisterComponent<PersistentIdComponent>();
            Scene.Components.AddResource<PersistentEntityIndex>();
            InstallEquipmentVocabulary(Scene.Components);
            Scene.Components.AddResource<ItemRegistry>();
            if (!firstItem.empty())
                (void)Scene.Components.GetResource<ItemRegistry>().Reserve(firstItem);
            GameplayTagRegistry& tags = Scene.Components.AddResource<GameplayTagRegistry>();
            for (const char* name : { "Item.Weapon", "Item.Ring", "State.Attacking" })
                (void)tags.RegisterTag(name);
            EquipmentLayout layout;
            std::vector<std::string> errors;
            EXPECT_TRUE(ResolveEquipmentLayoutDeclaration(*Compile(kHumanoid), Scene.Components, layout, errors));
            Humanoid = Scene.Components.GetResource<EquipmentLayoutRegistry>().Define(
                "asset://equipment/humanoid.sdata", std::move(layout));
        }
    };
}

TEST(EquipmentScene, ARecordIsSavedByPathSlotNameAndIdentity)
{
    SceneWorld first;
    World& world = first.Scene.Components;
    const ItemId ring = world.GetResource<ItemRegistry>().Reserve("asset://items/ring.sdata");
    const EntityId wearer = world.CreateEntity();
    EquipmentState state{ .Layout = first.Humanoid, .Count = 1, .NextKey = 8, .Records = {} };
    state.Records[0] = EquipmentRecord{ .Key = 7, .Definition = ring, .Slots = (1u << 2) | (1u << 1), .Primary = 2,
                                        .Source = { .Entity = {}, .Target = PersistentEntityId{ 0x5A0D } },
                                        .Derivation = EquipmentDerivation::Maintained };
    world.AddComponent<EquipmentState>(wearer, state);
    const std::unique_ptr<IComponentSerializer> serializer = MakeEquipmentStateSerializer();
    JsonWriteArchive written;
    ASSERT_TRUE(serializer->Save(written, wearer, first.Scene.Components, first.Context));
    const JsonValue saved = written.TakeValue();

    SceneWorld second("asset://items/other.sdata");
    World& loaded = second.Scene.Components;
    const EntityId copy = loaded.CreateEntity();
    JsonReadArchive read(saved);
    ASSERT_TRUE(serializer->LoadIntoWorld(read, copy, loaded, second.Context));
    const EquipmentState* restored = loaded.TryGet<EquipmentState>(copy);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(loaded.GetResource<EquipmentLayoutRegistry>().GetName(restored->Layout), "asset://equipment/humanoid.sdata");
    EXPECT_EQ(restored->NextKey, 8u);
    ASSERT_EQ(restored->Count, 1u);
    const EquipmentRecord& record = restored->Records[0];
    EXPECT_EQ(record.Key, 7u);
    EXPECT_EQ(loaded.GetResource<ItemRegistry>().GetName(record.Definition), "asset://items/ring.sdata");
    EXPECT_EQ(record.Slots, (1u << 2) | (1u << 1));
    EXPECT_EQ(record.Primary, 2u) << "the primary slot is saved first";
    EXPECT_EQ(record.Source.Target, PersistentEntityId{ 0x5A0D });
    EXPECT_EQ(record.Derivation, EquipmentDerivation::Pending) << "derived, so a load derives it again";
}

TEST(EquipmentScene, ARecordNamingASlotTheLayoutLacksRefusesTheScene)
{
    SceneWorld world;
    const std::unique_ptr<IComponentSerializer> serializer = MakeEquipmentStateSerializer();
    const auto loads = [&](std::string_view json) {
        const std::optional<JsonValue> value = JsonParse(json);
        JsonReadArchive read(*value);
        return serializer->LoadIntoWorld(read, world.Scene.Components.CreateEntity(), world.Scene.Components,
                                         world.Context);
    };
    EXPECT_TRUE(loads(R"({ "layout": "asset://equipment/humanoid.sdata", "next_key": 2,
        "records": [ { "key": 1, "definition": "asset://items/ring.sdata", "slots": [ "ring.left" ] } ] })"));
    EXPECT_FALSE(loads(R"({ "layout": "asset://equipment/humanoid.sdata", "next_key": 2,
        "records": [ { "key": 1, "definition": "asset://items/ring.sdata", "slots": [ "tail" ] } ] })"));
    EXPECT_FALSE(loads(R"({ "layout": "asset://equipment/missing.sdata", "next_key": 2,
        "records": [ { "key": 1, "definition": "asset://items/ring.sdata", "slots": [ "ring.left" ] } ] })"))
        << "a layout this World does not define cannot say which slots those are";
    EXPECT_FALSE(loads(R"({ "layout": "asset://equipment/humanoid.sdata", "next_key": 1,
        "records": [ { "key": 1, "definition": "asset://items/ring.sdata", "slots": [ "ring.left" ] } ] })"))
        << "a key the next one could reuse";
    EXPECT_FALSE(loads(R"({ "layout": "asset://equipment/humanoid.sdata", "next_key": 2,
        "records": [ { "key": 1, "definition": "asset://items/ring.sdata", "slots": [] } ] })"))
        << "a record holds at least one slot";
    EXPECT_FALSE(loads(R"({ "layout": "asset://equipment/humanoid.sdata", "next_key": 3,
        "records": [ { "key": 1, "definition": "asset://items/ring.sdata", "slots": [ "ring.left" ] },
                     { "key": 1, "definition": "asset://items/ring.sdata", "slots": [ "ring.right" ] } ] })"))
        << "two records with one key";
    EXPECT_FALSE(loads(R"({ "layout": "asset://equipment/humanoid.sdata", "next_key": 3,
        "records": [ { "key": 1, "definition": "asset://items/ring.sdata", "slots": [ "ring.left" ] },
                     { "key": 2, "definition": "asset://items/ring.sdata", "slots": [ "ring.left" ] } ] })"))
        << "two records in one slot";
    EXPECT_FALSE(loads(R"({ "layout": "asset://equipment/humanoid.sdata", "next_key": 2,
        "records": [ { "key": 1, "definition": "asset://items/ring.sdata", "slots": [ "ring.left", "ring.left" ] } ] })"))
        << "one slot named twice";
}

TEST(EquipmentScene, ASceneWithMoreRecordsThanAWearerHoldsIsRefused)
{
    SceneWorld world;
    World& entities = world.Scene.Components;
    EquipmentLayout many;
    for (std::size_t i = 0; i <= kEquipmentRecordCapacity; ++i)
    {
        many.Slots.emplace_back();
        many.Slots.back().Key = "c" + std::to_string(i);
    }
    (void)entities.GetResource<EquipmentLayoutRegistry>().Define("asset://equipment/charms.sdata", std::move(many));
    const std::unique_ptr<IComponentSerializer> serializer = MakeEquipmentStateSerializer();
    const auto loads = [&](std::size_t records) {
        std::string json = R"({ "layout": "asset://equipment/charms.sdata", "next_key": 99, "records": [)";
        for (std::size_t i = 0; i < records; ++i)
            json += std::string(i == 0 ? "" : ",") + R"({ "key": )" + std::to_string(i + 1)
                + R"(, "definition": "asset://items/charm.sdata", "slots": [ "c)" + std::to_string(i) + R"(" ] })";
        const std::optional<JsonValue> value = JsonParse(json + "] }");
        JsonReadArchive read(*value);
        return serializer->LoadIntoWorld(read, entities.CreateEntity(), entities, world.Context);
    };
    EXPECT_TRUE(loads(kEquipmentRecordCapacity));
    EXPECT_FALSE(loads(kEquipmentRecordCapacity + 1));
}

namespace
{
    constexpr std::string_view kBody = R"({
        "slots": [ { "key": "hand.main", "accepts": "Item.Weapon", "action": "Input.PrimaryFire", "priority": 10 },
                   { "key": "hand.off", "accepts": "Item.Weapon" },
                   { "key": "ring.left", "accepts": "Item.Ring", "action": "Input.AltFire", "priority": 20 },
                   { "key": "ring.right", "accepts": "Item.Ring", "action": "Input.AltFire", "priority": 21 },
                   { "key": "use.x", "accepts": "Item.Usable", "on_depleted": "hold" } ],
        "lock_tags": [ "State.Attacking" ] })";

    constexpr std::string_view kInstant = R"({
        "phases": [ { "key": "p_go", "then": [ { "key": "t_end", "to": "end" } ] } ] })";

    WorldComponentSchema EngineSchema()
    {
        WorldComponentSchema schema;
        RegisterEngineRuntimeComponents(schema);
        schema.Seal();
        return schema;
    }

    // An authority World with a body layout, rings that grant a bolt, a
    // two-handed sword, and a hero holding them.
    class EquipmentTest : public ::testing::Test
    {
    protected:
        EquipmentTest()
            : Schema(EngineSchema())
            , Runtime(Schema)
        {
            World& world = Entities();
            RegisterAbilityKit(world);
            InstallInventoryVocabulary(world);
            InstallEquipmentVocabulary(world);
            GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
            Ring = *tags.RegisterTag("Item.Ring");
            Usable = *tags.RegisterTag("Item.Usable");
            Weapon = *tags.RegisterTag("Item.Weapon");
            Attacking = *tags.RegisterTag("State.Attacking");
            EquipmentLayout layout;
            std::vector<std::string> errors;
            EXPECT_TRUE(ResolveEquipmentLayoutDeclaration(*Compile(kBody), world, layout, errors));
            Body = world.GetResource<EquipmentLayoutRegistry>().Define("asset://equipment/body.sdata", std::move(layout));

            Bolt = DefineAbility("asset://abilities/bolt.sdata");
            Slash = DefineAbility("asset://abilities/slash.sdata");
            EffectDefinition lasting{};
            lasting.Duration = EffectDuration::Infinite;
            EffectRegistry& effects = world.GetResource<EffectRegistry>();
            Glow = effects.Define("asset://effects/glow.sdata", lasting);
            Edge = effects.Define("asset://effects/edge.sdata", lasting);
            (void)DeclareEffectGrants(world, Glow, { { .Key = "g_bolt", .Ability = "asset://abilities/bolt.sdata", .Override = {} } });
            (void)DeclareEffectGrants(world, Edge, { { .Key = "g_slash", .Ability = "asset://abilities/slash.sdata", .Override = {} },
                                                     { .Key = "g_parry", .Ability = "asset://abilities/slash.sdata",
                                                       .Override = { .Priority = 3 } } });

            ItemRegistry& items = world.GetResource<ItemRegistry>();
            RingItem = items.Define("asset://items/ring.sdata",
                                    ItemDefinition{ .Name = "Ring", .Tags = { Ring }, .Occupies = { "ring" },
                                                    .EquipEffects = { { Glow, 1.0f } },
                                                    .GrantParameters = { { .Key = "g_bolt",
                                                                           .Parameters = { { .Key = "power", .Value = 7.0 } } } } });
            Greatsword = items.Define("asset://items/greatsword.sdata",
                                      ItemDefinition{ .Name = "Greatsword", .Tags = { Weapon },
                                                      .Occupies = { "hand.main", "hand.off" }, .EquipEffects = { { Edge, 1.0f } } });
            Pebble = items.Define("asset://items/pebble.sdata", ItemDefinition{ .Name = "Pebble" });
            Bomb = items.Define("asset://items/bomb.sdata",
                                ItemDefinition{ .Name = "Bomb", .Tags = { Usable }, .StackMax = 5,
                                                .Occupies = { "use.x" }, .EquipEffects = { { Glow, 1.0f } } });

            Hero = world.CreateEntity();
            world.AddComponent<ItemContainer>(Hero, ItemContainer{});
            world.AddComponent<GameplayTagContainer>(Hero, GameplayTagContainer{});
            world.AddComponent<AbilityGrants>(Hero, AbilityGrants{});
            world.AddComponent<EquipmentState>(Hero, EquipmentState{ .Layout = Body, .Count = 0, .NextKey = 1, .Records = {} });
            Declare();
        }

        World& Entities() { return Runtime.Entities(); }

        // What content declaration does once items and layouts are defined, and
        // again each frame a definition reloads.
        void Declare()
        {
            std::vector<std::string> errors;
            DeclareWornGrantEntries(Entities(), errors);
            EXPECT_TRUE(errors.empty()) << errors.front();
        }

        AbilityId DefineAbility(std::string_view path, std::string_view json = kInstant)
        {
            AbilityDeclaration declaration;
            std::vector<AssetRef> dependencies;
            std::vector<AbilityDiagnostic> diagnostics;
            EXPECT_TRUE(CompileAbilityDeclaration(*JsonParse(json), declaration, dependencies, diagnostics))
                << (diagnostics.empty() ? std::string() : diagnostics.front().Message);
            AbilityDefinition definition;
            EXPECT_TRUE(ResolveAbilityDeclaration(declaration, path, MakeAbilityResolveEnvironment(Entities()),
                                                  definition, diagnostics));
            return Entities().GetResource<AbilityRegistry>().Define(path, std::move(definition));
        }

        EntityId Give(ItemId item)
        {
            return Inventory(Entities()).Give(Hero, item, 1).Entry;
        }

        // One fixed tick, in schedule order: actor time, the ability pass, then the
        // equipment's and the inventory's.
        void Step()
        {
            Declare();
            const FixedSimTime now{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = Now++ };
            AdvanceActorTime(Entities(), now.TickIndex);
            RunAbilityPass(Entities(), AbilityHost{}, now);
            RunEquipmentPass(Entities(), now);
            RunInventoryPass(Entities());
        }

        Equipment Gear() { return Equipment(Entities(), FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = Now }); }

        const EquipmentState& State() { return *std::as_const(Entities()).TryGet<EquipmentState>(Hero); }

        // The equipment pass, then the inventory's, as the schedule orders them.
        void Pass()
        {
            Declare();
            RunEquipmentPass(Entities(), FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = Now });
            RunInventoryPass(Entities());
        }

        std::vector<const AbilityGrant*> GrantsOf(AbilityId ability)
        {
            std::vector<const AbilityGrant*> found;
            const AbilityGrants& grants = *std::as_const(Entities()).TryGet<AbilityGrants>(Hero);
            for (std::uint8_t i = 0; i < grants.Count; ++i)
                if (grants.Entries[i].Ability == ability)
                    found.push_back(&grants.Entries[i]);
            return found;
        }

        std::size_t EffectsOn()
        {
            std::size_t count = 0;
            Entities().ForEachComponent<ActiveEffect>([&](EntityId, const ActiveEffect& effect) { count += effect.Target == Hero; });
            return count;
        }

        WorldComponentSchema Schema;
        RuntimeWorld Runtime;
        GameplayTagId Ring, Usable, Weapon, Attacking;
        EquipmentLayoutId Body;
        AbilityId Bolt, Slash;
        EffectId Glow, Edge;
        ItemId RingItem, Greatsword, Pebble, Bomb;
        EntityId Hero;
        std::uint64_t Now = 0;
    };
}

TEST_F(EquipmentTest, TwoRingsWearTheirSlotsBindingsAndTheItemsParameters)
{
    const EntityId first = Give(RingItem);
    const EntityId second = Give(RingItem);
    ASSERT_EQ(Gear().Equip(Hero, first).Refusal, EquipmentRefusal::None);
    ASSERT_EQ(Gear().Equip(Hero, second).Refusal, EquipmentRefusal::None) << "the group's next free slot";
    ASSERT_EQ(State().Count, 2u);
    EXPECT_EQ(State().Records[0].Primary, 2u);
    EXPECT_EQ(State().Records[1].Primary, 3u);
    Step();

    const std::vector<const AbilityGrant*> bolts = GrantsOf(Bolt);
    ASSERT_EQ(bolts.size(), 2u);
    const AbilityGrantEntries& entries = Entities().GetResource<AbilityGrantEntries>();
    std::vector<std::int32_t> priorities;
    for (const AbilityGrant* grant : bolts)
    {
        priorities.push_back(entries.Binding(grant->Entry)->Priority);
        EXPECT_EQ(entries.Find(grant->Entry)->Parameters,
                  (std::vector<AbilityGrantParameter>{ { .Key = "power", .Value = 7.0 } }));
    }
    std::ranges::sort(priorities);
    EXPECT_EQ(priorities, (std::vector<std::int32_t>{ 20, 21 })) << "each ring binds as its slot says";
}

TEST_F(EquipmentTest, ATwoHandedItemIsOneRecordInTwoSlots)
{
    const EntityId sword = Give(Greatsword);
    const EquipmentResult equipped = Gear().Equip(Hero, sword);
    ASSERT_EQ(equipped.Refusal, EquipmentRefusal::None);
    ASSERT_EQ(State().Count, 1u);
    EXPECT_EQ(State().Records[0].Slots, 0b11u);
    EXPECT_EQ(State().Records[0].Primary, 0u);
    EXPECT_EQ(ResolvePersistableRef(Entities(), State().Records[0].Source), sword);
    EXPECT_TRUE(State().Records[0].Source.Target.IsValid()) << "a record's source is persistable";

    ASSERT_EQ(Gear().Unequip(Hero, "hand.off").Refusal, EquipmentRefusal::None) << "either slot names it";
    EXPECT_EQ(State().Count, 0u);
    EXPECT_EQ(EffectsOn(), 0u);
}

TEST_F(EquipmentTest, ASwapAtAFullGrantTableFitsWhatItReplacesAndNoMore)
{
    // Fill the table to two short of full with grants nothing will release.
    World& world = Entities();
    AbilityGrants& filling = *world.TryGet<AbilityGrants>(Hero);
    const std::uint16_t filler = world.GetResource<AbilityGrantEntries>().Intern({ "g_fill", Slash, {} });
    while (filling.Count < kAbilityGrantCapacity - 2)
        (void)GrantAbility(filling, Slash, AbilityGrantKind::Authored, filler);
    const EntityId sword = Give(Greatsword);
    ASSERT_EQ(Gear().Equip(Hero, sword).Refusal, EquipmentRefusal::None) << "two grants into two free places";
    Step();
    ASSERT_EQ(std::as_const(world).TryGet<AbilityGrants>(Hero)->Count, kAbilityGrantCapacity);

    // Another greatsword gives two for the two it replaces.
    const EntityId other = Give(Greatsword);
    ASSERT_EQ(Gear().Equip(Hero, other).Refusal, EquipmentRefusal::None) << "two for two";
    Step();
    EXPECT_EQ(ResolvePersistableRef(world, State().Records[0].Source), other);

    // A ring in a free slot replaces nothing, so its grant finds no room.
    const EntityId ring = Give(RingItem);
    const EquipmentState before = State();
    EXPECT_EQ(Gear().Equip(Hero, ring).Refusal, EquipmentRefusal::GrantRoom);
    EXPECT_EQ(std::memcmp(&before, &State(), sizeof(EquipmentState)), 0) << "a refusal changes nothing";
}

TEST_F(EquipmentTest, EveryRefusalLeavesTheWorldAsItWas)
{
    const EntityId ring = Give(RingItem);
    const EntityId pebble = Give(Pebble);
    const EntityId stranger = Entities().CreateEntity();
    Entities().AddComponent<ItemContainer>(stranger, ItemContainer{});
    const EntityId theirs = Inventory(Entities()).Give(stranger, RingItem, 1).Entry;
    const auto refusal = [&](EntityId entry, std::string_view slot = {}) {
        const EquipmentState before = State();
        const std::size_t effects = EffectsOn();
        const EquipmentRefusal result = Gear().Equip(Hero, entry, slot).Refusal;
        EXPECT_EQ(std::memcmp(&before, &State(), sizeof(EquipmentState)), 0);
        EXPECT_EQ(EffectsOn(), effects);
        return result;
    };
    EXPECT_EQ(refusal(EntityId{}), EquipmentRefusal::Missing);
    EXPECT_EQ(refusal(theirs), EquipmentRefusal::NotPermitted);
    EXPECT_EQ(refusal(pebble), EquipmentRefusal::NotEquippable);
    EXPECT_EQ(refusal(ring, "hand.main"), EquipmentRefusal::NoSlot) << "a ring does not name the hand";
    EXPECT_EQ(refusal(ring, "ring.thumb"), EquipmentRefusal::NoSlot);
    Entities().TryGet<GameplayTagContainer>(Hero)->Grant(Attacking);
    EXPECT_EQ(refusal(ring), EquipmentRefusal::Locked);
    Entities().TryGet<GameplayTagContainer>(Hero)->Revoke(Attacking);
    ASSERT_EQ(Gear().Equip(Hero, ring).Refusal, EquipmentRefusal::None);
    EXPECT_EQ(refusal(ring), EquipmentRefusal::AlreadyEquipped);

    ItemRegistry& items = Entities().GetResource<ItemRegistry>();
    const ItemId bracelet = items.Define("asset://items/bracelet.sdata",
                                         ItemDefinition{ .Name = "Bracelet", .Occupies = { "ring" } });
    EXPECT_EQ(refusal(Give(bracelet)), EquipmentRefusal::NotAccepted) << "it carries no ring tag";
}

TEST_F(EquipmentTest, AnEquipWhoseTagsFindNoRoomInTheWearersContainerIsRefused)
{
    World& world = Entities();
    GameplayTagRegistry& names = world.GetResource<GameplayTagRegistry>();
    EffectDefinition marked{};
    marked.Duration = EffectDuration::Infinite;
    marked.GrantedTags = { *names.RegisterTag("Mark.A"), *names.RegisterTag("Mark.B") };
    const EffectId marks = world.GetResource<EffectRegistry>().Define("asset://effects/marks.sdata", marked);
    const ItemId band = world.GetResource<ItemRegistry>().Define(
        "asset://items/band.sdata",
        ItemDefinition{ .Name = "Band", .Tags = { Ring }, .Occupies = { "ring" }, .EquipEffects = { { marks, 1.0f } } });
    Declare();
    GameplayTagContainer& tags = *world.TryGet<GameplayTagContainer>(Hero);
    for (int i = 0; tags.Count < GameplayTagContainer::Capacity - 1; ++i)
        (void)tags.GrantBaseline(*names.RegisterTag("Fill.T" + std::to_string(i)));
    const EntityId first = Give(band);
    const EquipmentState before = State();
    EXPECT_EQ(Gear().Equip(Hero, first, "ring.left").Refusal, EquipmentRefusal::TagRoom) << "two tags, one place";
    EXPECT_EQ(std::memcmp(&before, &State(), sizeof(EquipmentState)), 0);
    EXPECT_EQ(EffectsOn(), 0u);

    (void)world.TryGet<GameplayTagContainer>(Hero)->RevokeBaseline(names.FindTag("Fill.T0"));
    ASSERT_EQ(Gear().Equip(Hero, first, "ring.left").Refusal, EquipmentRefusal::None) << "two tags, two places";
    EXPECT_EQ(std::as_const(world).TryGet<GameplayTagContainer>(Hero)->Count, GameplayTagContainer::Capacity);
    EXPECT_EQ(Gear().Equip(Hero, Give(band), "ring.right").Refusal, EquipmentRefusal::None) << "tags it already holds";
    EXPECT_EQ(Gear().Equip(Hero, Give(band), "ring.left").Refusal, EquipmentRefusal::None)
        << "what it replaces leaves the room it takes";
}

TEST_F(EquipmentTest, AnEquipWhoseEffectModifiesAnAttributeTheWearerLacksIsRefused)
{
    World& world = Entities();
    const AttributeId might = world.GetResource<AttributeRegistry>().RegisterAttribute("Might", 0.0f, 100.0f, 0.0f);
    EffectDefinition strong{};
    strong.Duration = EffectDuration::Infinite;
    strong.Modifiers = { { might, ModifierOp::Add, 4.0f } };
    const EffectId strength = world.GetResource<EffectRegistry>().Define("asset://effects/strength.sdata", strong);
    const ItemId band = world.GetResource<ItemRegistry>().Define(
        "asset://items/might_band.sdata",
        ItemDefinition{ .Name = "Band", .Tags = { Ring }, .Occupies = { "ring" }, .EquipEffects = { { strength, 1.0f } } });
    Declare();
    const EntityId entry = Give(band);
    const EquipmentState before = State();
    EXPECT_EQ(Gear().Equip(Hero, entry).Refusal, EquipmentRefusal::MissingAttribute) << "no attribute set at all";
    world.AddComponent<AttributeSet>(Hero, AttributeSet{});
    EXPECT_EQ(Gear().Equip(Hero, entry).Refusal, EquipmentRefusal::MissingAttribute);
    EXPECT_EQ(std::memcmp(&before, &State(), sizeof(EquipmentState)), 0);
    EXPECT_EQ(EffectsOn(), 0u);

    (void)world.TryGet<AttributeSet>(Hero)->Add(might, 1.0f);
    EXPECT_EQ(Gear().Equip(Hero, entry).Refusal, EquipmentRefusal::None);
}

TEST_F(EquipmentTest, AWearerHoldsSixteenRecordsAndRefusesTheSeventeenth)
{
    World& world = Entities();
    EquipmentLayout many;
    for (std::size_t i = 0; i <= kEquipmentRecordCapacity; ++i)
    {
        many.Slots.emplace_back();
        many.Slots.back().Key = "charm.c" + std::to_string(i);
    }
    const EquipmentLayoutId layout =
        world.GetResource<EquipmentLayoutRegistry>().Define("asset://equipment/charms.sdata", std::move(many));
    const ItemId charm = world.GetResource<ItemRegistry>().Define(
        "asset://items/charm.sdata", ItemDefinition{ .Name = "Charm", .Occupies = { "charm" } });
    world.TryGet<EquipmentState>(Hero)->Layout = layout;
    for (std::size_t i = 0; i < kEquipmentRecordCapacity; ++i)
        ASSERT_EQ(Gear().Equip(Hero, Give(charm)).Refusal, EquipmentRefusal::None) << i;
    ASSERT_EQ(State().Count, kEquipmentRecordCapacity);

    const EntityId extra = Give(charm);
    const EquipmentState before = State();
    EXPECT_EQ(Gear().Equip(Hero, extra).Refusal, EquipmentRefusal::Full) << "a free slot, and no record to hold it";
    EXPECT_EQ(std::memcmp(&before, &State(), sizeof(EquipmentState)), 0);
    EXPECT_EQ(Gear().Equip(Hero, extra, "charm.c0").Refusal, EquipmentRefusal::None) << "a swap needs no new record";
}

TEST_F(EquipmentTest, AnEquipFillsTheSixteenthGrantAndRefusesTheSeventeenth)
{
    World& world = Entities();
    AbilityGrants& filling = *world.TryGet<AbilityGrants>(Hero);
    const std::uint16_t filler = world.GetResource<AbilityGrantEntries>().Intern({ "g_fill", Slash, {} });
    while (filling.Count < kAbilityGrantCapacity - 1)
        (void)GrantAbility(filling, Slash, AbilityGrantKind::Authored, filler);
    ASSERT_EQ(Gear().Equip(Hero, Give(RingItem), "ring.left").Refusal, EquipmentRefusal::None) << "the sixteenth";
    EXPECT_EQ(Gear().Equip(Hero, Give(RingItem), "ring.right").Refusal, EquipmentRefusal::GrantRoom)
        << "reserved before the pass grants it";
    Step();
    EXPECT_EQ(std::as_const(world).TryGet<AbilityGrants>(Hero)->Count, kAbilityGrantCapacity);
    EXPECT_EQ(Gear().Equip(Hero, Give(RingItem), "ring.right").Refusal, EquipmentRefusal::GrantRoom);
}

TEST_F(EquipmentTest, ReconcilingTwiceChangesNothingAndRestoresWhatWasLost)
{
    const EntityId sword = Give(Greatsword);
    ASSERT_EQ(Gear().Equip(Hero, sword).Refusal, EquipmentRefusal::None);
    const FixedSimTime now{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = Now };
    EXPECT_EQ(ReconcileEquipment(Entities(), Hero, now), 0u);
    EXPECT_EQ(ReconcileEquipment(Entities(), Hero, now), 0u);
    EXPECT_EQ(EffectsOn(), 1u) << "applied once, not once a reconcile";

    // A restore brings the record back without what it maintained.
    (void)RemoveEffectsOwnedBy(Entities(), EquipmentRecordOwner(sword, State().Records[0].Key));
    EXPECT_EQ(EffectsOn(), 0u);
    EXPECT_EQ(ReconcileEquipment(Entities(), Hero, now), 0u);
    EXPECT_EQ(EffectsOn(), 1u);
    Step();
    EXPECT_EQ(GrantsOf(Slash).size(), 2u);
}

TEST_F(EquipmentTest, CyclingWalksTheContainerAndUnequipsWhenNothingElseFits)
{
    const EntityId first = Give(Greatsword);
    (void)Give(Pebble);
    const EntityId second = Give(Greatsword);
    ASSERT_EQ(Gear().Cycle(Hero, "hand").Refusal, EquipmentRefusal::None);
    EXPECT_EQ(ResolvePersistableRef(Entities(), State().Records[0].Source), first);
    ASSERT_EQ(Gear().Cycle(Hero, "hand").Refusal, EquipmentRefusal::None);
    EXPECT_EQ(ResolvePersistableRef(Entities(), State().Records[0].Source), second) << "the pebble is passed over";
    ASSERT_EQ(Gear().Cycle(Hero, "hand").Refusal, EquipmentRefusal::None);
    EXPECT_EQ(ResolvePersistableRef(Entities(), State().Records[0].Source), first) << "it wraps";
    ASSERT_EQ(State().Count, 1u);
}

TEST_F(EquipmentTest, MovingAWornEntryUnequipsItUnlessItsWearerIsLocked)
{
    const EntityId sword = Give(Greatsword);
    const EntityId ring = Give(RingItem);
    ASSERT_EQ(Gear().Equip(Hero, sword).Refusal, EquipmentRefusal::None);
    ASSERT_EQ(Gear().Equip(Hero, ring).Refusal, EquipmentRefusal::None);
    const EntityId chest = Entities().CreateEntity();
    Entities().AddComponent<ItemContainer>(chest, ItemContainer{});
    Inventory inventory(Entities());

    Entities().TryGet<GameplayTagContainer>(Hero)->Grant(Attacking);
    EXPECT_EQ(inventory.Transfer(sword, chest, 0).Refusal, InventoryRefusal::Withheld);
    EXPECT_EQ(inventory.Drop(ring, Transform3f::Identity(), StoragePartitionId::Default()).Refusal,
              InventoryRefusal::Withheld);
    EXPECT_EQ(State().Count, 2u);
    Entities().TryGet<GameplayTagContainer>(Hero)->Revoke(Attacking);

    ASSERT_EQ(inventory.Transfer(sword, chest, 0).Refusal, InventoryRefusal::None);
    EXPECT_EQ(State().Count, 1u) << "unequipped in the move itself";
    ASSERT_EQ(inventory.Drop(ring, Transform3f::Identity(), StoragePartitionId::Default()).Refusal,
              InventoryRefusal::None);
    EXPECT_EQ(State().Count, 0u);
    EXPECT_EQ(EffectsOn(), 0u);
}

TEST_F(EquipmentTest, ASpentSourceReleasesOrHoldsAsItsSlotSays)
{
    const EntityId ring = Give(RingItem);
    const EntityId bomb = Give(Bomb);
    ASSERT_EQ(Gear().Equip(Hero, ring).Refusal, EquipmentRefusal::None);
    ASSERT_EQ(Gear().Equip(Hero, bomb).Refusal, EquipmentRefusal::None);
    for (const EntityId spent : { ring, bomb })
    {
        Entities().TryGet<ItemEntry>(spent)->Count = 0;
        NoteDepleted(Entities(), spent, EntityId{});
    }
    Pass();
    ASSERT_EQ(State().Count, 1u) << "the ring's slot releases";
    const EquipmentRecord& held = State().Records[0];
    EXPECT_EQ(held.Definition, Bomb);
    EXPECT_FALSE(ResolvePersistableRef(Entities(), held.Source).IsValid()) << "the bomb's slot holds, sourceless";
    EXPECT_EQ(EffectsOn(), 0u) << "a record with no source maintains nothing";
    EXPECT_FALSE(Entities().IsAlive(bomb)) << "the spent entry is reaped all the same";

    const EntityId more = Give(Bomb);
    Pass();
    EXPECT_EQ(ResolvePersistableRef(Entities(), State().Records[0].Source), more) << "a pickup takes the held slot";
    EXPECT_EQ(EffectsOn(), 1u) << "and maintains from it";
}

// A held slot's source came back under its id as another entity (a zone's
// round trip) and was spent: what the old handle maintained ends in the same
// pass, not a tick later when the effect pass sees its owner gone.
TEST_F(EquipmentTest, AHoldingSlotEndsWhatItsSourcesOldHandleMaintained)
{
    World& world = Entities();
    const EntityId bomb = Give(Bomb);
    ASSERT_EQ(Gear().Equip(Hero, bomb).Refusal, EquipmentRefusal::None);
    ASSERT_EQ(EffectsOn(), 1u);
    const PersistentEntityId id = std::as_const(world).TryGet<PersistentIdComponent>(bomb)->Id;
    world.TryGet<ItemEntry>(bomb)->Count = 5;
    const EntityId again = Give(Bomb);
    ASSERT_NE(again, bomb);
    world.DestroyEntity(bomb);
    world.RemoveComponent<PersistentIdComponent>(again);
    world.AddComponent<PersistentIdComponent>(again, PersistentIdComponent{ .Id = id });
    world.TryGet<ItemEntry>(again)->Count = 0;
    NoteDepleted(world, again, EntityId{});
    Pass();
    ASSERT_EQ(State().Count, 1u) << "use.x holds";
    EXPECT_EQ(EffectsOn(), 0u) << "nothing is maintained from either handle";
}

TEST_F(EquipmentTest, ADestroyedSourceEndsItsRecord)
{
    const EntityId sword = Give(Greatsword);
    ASSERT_EQ(Gear().Equip(Hero, sword).Refusal, EquipmentRefusal::None);
    Entities().DestroyEntity(sword);
    Pass();
    EXPECT_EQ(State().Count, 0u);
    EXPECT_EQ(EffectsOn(), 0u);
}

// A record content authored (or a save restored) maintains nothing until a pass
// derives it, and is derived once.
TEST_F(EquipmentTest, AnAuthoredRecordIsReconciledOnce)
{
    const EntityId sword = Give(Greatsword);
    EquipmentState& state = *Entities().TryGet<EquipmentState>(Hero);
    state.Records[0] = EquipmentRecord{ .Key = 1, .Definition = Greatsword, .Slots = 0b11u, .Primary = 0,
                                        .Source = MakePersistableRef(Entities(), sword),
                                        .Derivation = EquipmentDerivation::Pending };
    state.Count = 1;
    state.NextKey = 2;
    EXPECT_EQ(EffectsOn(), 0u);
    Pass();
    EXPECT_EQ(EffectsOn(), 1u);
    Pass();
    EXPECT_EQ(EffectsOn(), 1u);
    Step();
    EXPECT_EQ(GrantsOf(Slash).size(), 2u);
}

TEST_F(EquipmentTest, AReloadedDefinitionIsDerivedAgain)
{
    const EntityId ring = Give(RingItem);
    ASSERT_EQ(Gear().Equip(Hero, ring).Refusal, EquipmentRefusal::None);
    ItemDefinition stronger = *Entities().GetResource<ItemRegistry>().Get(RingItem);
    stronger.EquipEffects = { { Glow, 1.0f }, { Edge, 1.0f } };
    (void)Entities().GetResource<ItemRegistry>().Define("asset://items/ring.sdata", stronger);
    Pass();
    EXPECT_EQ(EffectsOn(), 2u);
    stronger.EquipEffects = { { Edge, 1.0f } };
    (void)Entities().GetResource<ItemRegistry>().Define("asset://items/ring.sdata", stronger);
    Pass();
    EXPECT_EQ(EffectsOn(), 1u);
    EXPECT_EQ(State().Records[0].Derivation, EquipmentDerivation::Maintained);
}

namespace
{
    class EquipmentVocabularyTest : public EquipmentTest
    {
    protected:
        EquipmentVocabularyTest()
        {
            InstallAuthoredVocabulary(Entities());
            EXPECT_TRUE(InstallEngineVocabulary(Entities()));
            Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(Entities()));
            Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(Entities()));
            const VerbRegistry& catalog = Verbs->Registry();
            Tokens.push_back(Verbs->Bind<&InvokeEquipmentEquip>(catalog.Find(kEquipmentEquipVerb), Operations));
            Tokens.push_back(Verbs->Bind<&InvokeEquipmentUnequip>(catalog.Find(kEquipmentUnequipVerb), Operations));
            Tokens.push_back(Verbs->Bind<&InvokeEquipmentCycle>(catalog.Find(kEquipmentCycleVerb), Operations));
            QueryTokens = BindEquipmentQueries(*Queries, std::as_const(Entities()));
        }

        // `verb` with its entity arguments as inputs, in order, and the rest as literal text.
        VerbAdmission Invoke(std::string_view verb, std::vector<std::pair<std::string, EntityId>> entities,
                             std::vector<std::pair<std::string, std::string>> literals = {})
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
            for (const auto& [name, text] : literals)
            {
                VerbBindingArgument literal;
                literal.Key = name;
                literal.Literal = JsonValue(text);
                desc.Arguments.push_back(std::move(literal));
            }
            CompiledVerbBinding binding;
            std::vector<std::string> errors;
            EXPECT_TRUE(CompileVerbBinding(desc, MakeVerbBindingEnvironment(Entities()), binding, errors))
                << (errors.empty() ? "" : errors.front());
            return Verbs->Invoke(binding, inputs).Status;
        }

        // `verb` on `wearer`'s `slot`, with `force` set: what the World decides.
        VerbAdmission InvokeForced(std::string_view verb, EntityId wearer, std::string_view slot)
        {
            VerbBindingDesc desc;
            desc.Key = std::string(verb) + ".forced";
            desc.KeyId = MakeVerbBindingKey(desc.Key);
            desc.VerbName = std::string(verb);
            desc.Inputs.push_back("wearer");
            VerbBindingArgument input;
            input.Key = "wearer";
            input.Source = VerbArgumentSource::Input;
            input.Text = "wearer";
            desc.Arguments.push_back(std::move(input));
            for (auto [key, value] : { std::pair{ "slot", JsonValue(std::string(slot)) }, std::pair{ "force", JsonValue(true) } })
            {
                VerbBindingArgument literal;
                literal.Key = key;
                literal.Literal = std::move(value);
                desc.Arguments.push_back(std::move(literal));
            }
            CompiledVerbBinding binding;
            std::vector<std::string> errors;
            EXPECT_TRUE(CompileVerbBinding(desc, MakeVerbBindingEnvironment(Entities()), binding, errors))
                << (errors.empty() ? "" : errors.front());
            const std::vector<AuthoredValue> inputs{ AuthoredValue::Entity(wearer) };
            return Verbs->Invoke(binding, inputs).Status;
        }

        AuthoredQueryStatus Ask(std::string_view query, std::vector<AuthoredValue> arguments, AuthoredValue& result)
        {
            return Queries->Evaluate(Queries->Registry().Resolve(query), arguments, result);
        }

        FixedSimulationLoop Clock;
        EquipmentOperations Operations{ Entities(), Clock };
        std::unique_ptr<VerbDispatcher> Verbs;
        std::unique_ptr<AuthoredQueryDispatcher> Queries;
        std::vector<VerbBindingToken> Tokens;
        std::vector<AuthoredQueryBindingToken> QueryTokens;
    };
}

TEST_F(EquipmentVocabularyTest, TheVerbsChangeWhatTheQueriesSee)
{
    const EntityId ring = Give(RingItem);
    const EntityId sword = Give(Greatsword);
    EXPECT_EQ(Invoke(kEquipmentEquipVerb, { { "wearer", Hero }, { "entry", ring } }, { { "slot", "ring.right" } }),
              VerbAdmission::Accepted);
    AuthoredValue worn;
    ASSERT_EQ(Ask(kEquipmentInSlotQuery, { AuthoredValue::Entity(Hero), AuthoredValue::String("ring.right") }, worn),
              AuthoredQueryStatus::Value);
    EntityId entry;
    ASSERT_TRUE(worn.TryGetEntity(entry));
    EXPECT_EQ(entry, ring);
    EXPECT_EQ(Ask(kEquipmentInSlotQuery, { AuthoredValue::Entity(Hero), AuthoredValue::String("ring.left") }, worn),
              AuthoredQueryStatus::Unavailable) << "an empty slot has no answer";

    EXPECT_EQ(Invoke(kEquipmentCycleVerb, { { "wearer", Hero } }, { { "group", "hand" } }), VerbAdmission::Accepted);
    AuthoredValue rings;
    ASSERT_EQ(Ask(kEquipmentSourcesQuery, { AuthoredValue::Entity(Hero), AuthoredValue::Tag(Ring) }, rings),
              AuthoredQueryStatus::Value);
    ASSERT_EQ(rings.Children().size(), 1u);
    ASSERT_TRUE(rings.Children()[0].TryGetEntity(entry));
    EXPECT_EQ(entry, ring);
    ASSERT_EQ(Ask(kEquipmentInSlotQuery, { AuthoredValue::Entity(Hero), AuthoredValue::String("hand.off") }, worn),
              AuthoredQueryStatus::Value);
    ASSERT_TRUE(worn.TryGetEntity(entry));
    EXPECT_EQ(entry, sword) << "cycled in, both hands";

    EXPECT_EQ(Invoke(kEquipmentUnequipVerb, { { "wearer", Hero } }, { { "slot", "ring.right" } }), VerbAdmission::Accepted);
    EXPECT_EQ(Invoke(kEquipmentUnequipVerb, { { "wearer", Hero } }, { { "slot", "ring.right" } }), VerbAdmission::Refused)
        << "nothing is there to take off";
}

TEST_F(EquipmentVocabularyTest, AWornSlotTranslatesToTheItemsEntry)
{
    const EntityId bomb = Inventory(Entities()).Give(Hero, Bomb, 3).Entry;
    ASSERT_EQ(Gear().Equip(Hero, bomb).Refusal, EquipmentRefusal::None);
    const std::vector<AuthoredValue> two{ AuthoredValue::String("use.x"), AuthoredValue::Int(2) };
    const std::vector<AuthoredValue> empty{ AuthoredValue::String("ring.left"), AuthoredValue::Int(1) };
    std::vector<AuthoredValue> spent;
    ASSERT_TRUE(TranslateSlotCost(Entities(), AbilityCostRow{ .Actor = Hero, .Source = {}, .Arguments = two }, spent));
    ASSERT_EQ(spent.size(), 3u) << "inventory.item's entry, container and count";
    EntityId entry;
    ASSERT_TRUE(spent[0].TryGetEntity(entry));
    EXPECT_EQ(entry, bomb);
    std::int64_t count = 0;
    ASSERT_TRUE(spent[2].TryGetInt(count));
    EXPECT_EQ(count, 2);
    EXPECT_FALSE(TranslateSlotCost(Entities(), AbilityCostRow{ .Actor = Hero, .Source = {}, .Arguments = empty }, spent))
        << "an empty slot cannot pay";
}

// Composition Stage 5's gate (docs/plans/composition-stage5-equipment.md, S5.9):
// the HL2, Skyrim and Zelda exercises, unequipping mid-ability, and a restore.
namespace
{
    constexpr std::string_view kLasting = R"({
        "owned_tags": [ "State.Attacking" ],
        "phases": [ { "key": "p_hold", "then": [ { "key": "t_done", "on": "timeout", "ticks": 3, "to": "end" } ] } ] })";

    // Spends a unit of its source, then takes three ticks to finish.
    constexpr std::string_view kThrow = R"({
        "costs": [ { "key": "g_unit", "resources": [ { "key": "r_unit", "provider": "inventory.item",
                                                      "arguments": { "entry": { "port": "source" } } } ] } ],
        "phases": [ { "key": "p_throw", "enter": [ { "key": "o_pay", "op": "commit", "groups": ["g_unit"] } ],
                      "then": [ { "key": "t_done", "on": "timeout", "ticks": 3, "to": "end" } ] } ] })";

    // Spends a unit of what its source holds as it starts: a bottle's contents.
    constexpr std::string_view kSip = R"({
        "costs": [ { "key": "g_sip", "when": "start",
                     "resources": [ { "key": "r_unit", "provider": "inventory.item",
                                      "arguments": { "container": { "port": "source" } } } ] } ],
        "phases": [ { "key": "p_sip", "then": [ { "key": "t_done", "to": "end" } ] } ] })";

    // A round from its source's own clip and one from the wearer's reserve, as one commit.
    constexpr std::string_view kFire = R"({
        "costs": [ { "key": "g_clip", "target": "source", "effects": [ { "data": "asset://effects/round.sdata" } ] },
                   { "key": "g_reserve", "effects": [ { "data": "asset://effects/round.sdata" } ] } ],
        "phases": [ { "key": "p_fire", "enter": [ { "key": "o_pay", "op": "commit", "groups": ["g_clip", "g_reserve"] } ],
                      "then": [ { "key": "t_done", "to": "end" } ] } ] })";

    class EquipmentGateTest : public EquipmentTest
    {
    protected:
        EquipmentGateTest()
        {
            World& world = Entities();
            Rounds = world.GetResource<AttributeRegistry>().RegisterAttribute("Rounds", 0.0f, 100.0f, 0.0f);
            EffectDefinition round{};
            round.Modifiers = { { Rounds, ModifierOp::Add, -1.0f } };
            EffectRegistry& effects = world.GetResource<EffectRegistry>();
            (void)effects.Define("asset://effects/round.sdata", round);
            Ward = DefineAbility("asset://abilities/ward.sdata", kLasting);
            Throw = DefineAbility("asset://abilities/throw.sdata", kThrow);
            Sip = DefineAbility("asset://abilities/sip.sdata", kSip);
            Fire = DefineAbility("asset://abilities/fire.sdata", kFire);
            const auto granting = [&](std::string_view path, std::string key, std::string_view ability) {
                EffectDefinition lasting{};
                lasting.Duration = EffectDuration::Infinite;
                const EffectId effect = effects.Define(path, lasting);
                (void)DeclareEffectGrants(world, effect, { { .Key = std::move(key), .Ability = std::string(ability), .Override = {} } });
                return effect;
            };
            ItemRegistry& items = world.GetResource<ItemRegistry>();
            WardRing = items.Define("asset://items/ward_ring.sdata",
                                    ItemDefinition{ .Name = "Ward Ring", .Tags = { Ring }, .Occupies = { "ring" },
                                                    .EquipEffects = { { granting("asset://effects/ward.sdata", "g_ward",
                                                                                 "asset://abilities/ward.sdata"), 1.0f } } });
            Grenade = items.Define("asset://items/grenade.sdata",
                                   ItemDefinition{ .Name = "Grenade", .Tags = { Usable }, .StackMax = 5, .Occupies = { "use.x" },
                                                   .EquipEffects = { { granting("asset://effects/throw.sdata", "g_throw",
                                                                                "asset://abilities/throw.sdata"), 1.0f } } });
            Bottle = items.Define("asset://items/bottle.sdata",
                                  ItemDefinition{ .Name = "Bottle", .Tags = { Usable }, .Occupies = { "use.x" },
                                                  .EquipEffects = { { granting("asset://effects/sip.sdata", "g_sip",
                                                                               "asset://abilities/sip.sdata"), 1.0f } } });
            Fairy = items.Define("asset://items/fairy.sdata", ItemDefinition{ .Name = "Fairy" });
            Pistol = items.Define("asset://items/pistol.sdata",
                                  ItemDefinition{ .Name = "Pistol", .Tags = { Weapon }, .Attributes = { { Rounds, 2.0f } },
                                                  .Occupies = { "hand.main" },
                                                  .EquipEffects = { { granting("asset://effects/fire.sdata", "g_fire",
                                                                               "asset://abilities/fire.sdata"), 1.0f } } });
            world.AddComponent<AttributeSet>(Hero);
            (void)world.TryGet<AttributeSet>(Hero)->Add(Rounds, 10.0f);
            Declare();
        }

        void Request(AbilityId ability)
        {
            Entities().GetResource<AbilityRequestQueue>().Push(
                { .Kind = AbilityRequestKind::Activate, .Actor = Hero, .Ability = ability });
        }

        // The grant behind the hero's one live activation of `ability`.
        const AbilityGrant* LiveGrantOf(AbilityId ability)
        {
            const AbilityGrants& grants = *std::as_const(Entities()).TryGet<AbilityGrants>(Hero);
            for (const AbilityRecord& record : AbilityRecordsOf(Entities(), Hero))
            {
                if (record.State != AbilityRecordState::Live || record.Key.Ability != ability)
                    continue;
                for (std::uint8_t g = 0; g < grants.Count; ++g)
                    if (grants.Entries[g].Id == record.Grant)
                        return &grants.Entries[g];
            }
            return nullptr;
        }

        std::vector<AbilityTraceRecord> Traced(AbilityTraceEvent event)
        {
            std::vector<AbilityTraceRecord> found;
            Entities().GetResource<AbilityTrace>().Records().ForEach([&](const AbilityTraceRecord& record) {
                if (record.Event == event)
                    found.push_back(record);
            });
            return found;
        }

        float Units(EntityId entity) { return std::as_const(Entities()).TryGet<AttributeSet>(entity)->GetBase(Rounds); }

        AttributeId Rounds;
        AbilityId Ward, Throw, Sip, Fire;
        ItemId WardRing, Grenade, Bottle, Fairy, Pistol;
    };
}

// Skyrim: two rings grant one ward; the right hand's priority casts, from the
// right ring, as its source.
TEST_F(EquipmentGateTest, RingsCastBySlotPriorityFromTheirOwnSource)
{
    const EntityId left = Give(WardRing);
    const EntityId right = Give(WardRing);
    ASSERT_EQ(Gear().Equip(Hero, left, "ring.left").Refusal, EquipmentRefusal::None);
    ASSERT_EQ(Gear().Equip(Hero, right, "ring.right").Refusal, EquipmentRefusal::None);
    Step();
    Request(Ward);
    Step();
    const AbilityGrant* cast = LiveGrantOf(Ward);
    ASSERT_NE(cast, nullptr);
    EXPECT_EQ(cast->Source, right) << "priority 21 over 20";
    EXPECT_EQ(Entities().GetResource<AbilityGrantEntries>().Binding(cast->Entry)->Priority, 21);
}

// Unequipping mid-ability: refused while the swing's own tag locks the
// wearer, and once allowed, the activation ends with its grant.
TEST_F(EquipmentGateTest, UnequippingMidAbilityIsLockedThenEndsItsActivation)
{
    const EntityId ring = Give(WardRing);
    ASSERT_EQ(Gear().Equip(Hero, ring).Refusal, EquipmentRefusal::None);
    Step();
    Request(Ward);
    Step();
    ASSERT_NE(LiveGrantOf(Ward), nullptr);
    EXPECT_EQ(Gear().Unequip(Hero, "ring.left").Refusal, EquipmentRefusal::Locked) << "State.Attacking is a lock tag";

    // Content can lift the lock and end it by force: the World decides, no lock applies.
    Entities().TryGet<GameplayTagContainer>(Hero)->Erase(Attacking);
    ASSERT_EQ(Gear().Unequip(Hero, "ring.left").Refusal, EquipmentRefusal::None);
    Step();
    EXPECT_EQ(LiveGrantOf(Ward), nullptr);
    const std::vector<AbilityTraceRecord> ended = Traced(AbilityTraceEvent::Ended);
    ASSERT_FALSE(ended.empty());
    EXPECT_EQ(ended.back().EndReason, AbilityEndReason::GrantRevoked);
}

// Zelda: the last bomb finishes its throw before its slot settles; the slot
// holds, and a pickup takes it. (A releasing slot is ASpentSourceReleasesOrHoldsAsItsSlotSays.)
TEST_F(EquipmentGateTest, TheLastBombFinishesItsThrowBeforeItsSlotHolds)
{
    const EntityId grenade = Inventory(Entities()).Give(Hero, Grenade, 1).Entry;
    ASSERT_EQ(Gear().Equip(Hero, grenade).Refusal, EquipmentRefusal::None);
    Step();
    Request(Throw);
    Step();
    ASSERT_EQ(std::as_const(Entities()).TryGet<ItemEntry>(grenade)->Count, 0u) << "the throw paid its unit";
    const AbilityGrant* throwing = LiveGrantOf(Throw);
    ASSERT_NE(throwing, nullptr);
    EXPECT_EQ(throwing->Source, grenade);
    EXPECT_EQ(State().Count, 1u) << "the record waits while the throw it supplied runs";
    for (int i = 0; i < 5; ++i)
        Step();
    EXPECT_TRUE(Traced(AbilityTraceEvent::Ended).back().EndReason != AbilityEndReason::GrantRevoked)
        << "the throw finished on its own";
    ASSERT_EQ(State().Count, 1u) << "use.x holds";
    EXPECT_FALSE(ResolvePersistableRef(Entities(), State().Records[0].Source).IsValid());
    EXPECT_FALSE(Entities().IsAlive(grenade));
    EXPECT_EQ(EffectsOn(), 0u);

    const EntityId more = Inventory(Entities()).Give(Hero, Grenade, 2).Entry;
    Step();
    EXPECT_EQ(ResolvePersistableRef(Entities(), State().Records[0].Source), more);
    EXPECT_EQ(EffectsOn(), 1u);
    Step();
    Request(Throw);
    Step();
    EXPECT_EQ(std::as_const(Entities()).TryGet<ItemEntry>(more)->Count, 1u) << "and throws again";
}

// Zelda: drinking from the bottle spends what it holds; the bottle stays worn,
// and the next sip is refused.
TEST_F(EquipmentGateTest, ABottleStaysWornWhileItsContentsAreDrunk)
{
    const EntityId bottle = Give(Bottle);
    Entities().AddComponent<ItemContainer>(bottle, ItemContainer{ .Capacity = 1, .Accepts = {} });
    const EntityId fairy = Inventory(Entities()).Give(bottle, Fairy, 1).Entry;
    ASSERT_EQ(Gear().Equip(Hero, bottle).Refusal, EquipmentRefusal::None);
    Step();
    Request(Sip);
    Step();
    EXPECT_FALSE(Entities().IsAlive(fairy)) << "drunk, and reaped once nothing runs from it";
    EXPECT_TRUE(ItemsIn(Entities(), bottle).empty());
    for (int i = 0; i < 3; ++i)
        Step();
    ASSERT_EQ(State().Count, 1u);
    EXPECT_EQ(ResolvePersistableRef(Entities(), State().Records[0].Source), bottle) << "the bottle stays assigned";
    const std::size_t refusedBefore = Traced(AbilityTraceEvent::Refused).size();
    Request(Sip);
    Step();
    ASSERT_GT(Traced(AbilityTraceEvent::Refused).size(), refusedBefore);
    EXPECT_EQ(Traced(AbilityTraceEvent::Refused).back().Refusal, AbilityRefusal::CostFailed);
}

// HL2: a round from the pistol's own clip and one from the reserve, in one
// commit; the source is the pistol, so its clip pays.
TEST_F(EquipmentGateTest, FireSpendsTheWeaponsClipAndTheReserveInOneCommit)
{
    const EntityId pistol = Give(Pistol);
    ASSERT_EQ(Gear().Equip(Hero, pistol).Refusal, EquipmentRefusal::None);
    Step();
    Request(Fire);
    Step();
    EXPECT_FLOAT_EQ(Units(pistol), 1.0f);
    EXPECT_FLOAT_EQ(Units(Hero), 9.0f);
    Step();
    Request(Fire);
    Step();
    Request(Fire);
    Step();
    EXPECT_FLOAT_EQ(Units(pistol), 0.0f);
    EXPECT_FLOAT_EQ(Units(Hero), 8.0f) << "a clip that cannot pay refuses the reserve its round too";
}

// A wearer saved and restored, then reconciled by the pass: the same tags,
// grants and attributes, with nothing contributed twice.
TEST_F(EquipmentGateTest, ARestoreReconcilesToWhatWasWorn)
{
    const EntityId sword = Give(Greatsword);
    const EntityId ring = Give(WardRing);
    ASSERT_EQ(Gear().Equip(Hero, sword).Refusal, EquipmentRefusal::None);
    ASSERT_EQ(Gear().Equip(Hero, ring).Refusal, EquipmentRefusal::None);
    Entities().TryGet<GameplayTagContainer>(Hero)->GrantBaseline(Ring);
    Step();

    LoggingProvider logging;
    SceneSerializationContext context{ logging };
    const std::unique_ptr<IComponentSerializer> equipment = MakeEquipmentStateSerializer();
    JsonWriteArchive equipped;
    ASSERT_TRUE(equipment->Save(equipped, Hero, Entities(), context));
    const JsonValue savedEquipment = equipped.TakeValue();
    JsonWriteArchive tagged;
    ASSERT_TRUE(WriteGameplayTags(tagged, *std::as_const(Entities()).TryGet<GameplayTagContainer>(Hero),
                                  Entities().GetResource<GameplayTagRegistry>()));
    const JsonValue savedTags = tagged.TakeValue();
    const GameplayTagContainer before = *std::as_const(Entities()).TryGet<GameplayTagContainer>(Hero);
    const std::size_t grantsBefore = std::as_const(Entities()).TryGet<AbilityGrants>(Hero)->Count;
    const std::size_t effectsBefore = EffectsOn();

    // The restore: what persisted comes back, what was derived does not.
    const EquipmentLayoutId layout = State().Layout;
    for (const EquipmentRecord& record : std::vector(State().Records, State().Records + State().Count))
        (void)RemoveEffectsOwnedBy(Entities(), EquipmentRecordOwner(record.Source.Entity, record.Key));
    Entities().RemoveComponent<EquipmentState>(Hero);
    Entities().TryGet<GameplayTagContainer>(Hero)->Clear();
    Step();
    ASSERT_EQ(EffectsOn(), 0u);
    JsonReadArchive readTags(savedTags);
    ASSERT_TRUE(ReadGameplayTags(readTags, *Entities().TryGet<GameplayTagContainer>(Hero),
                                 Entities().GetResource<GameplayTagRegistry>()));
    JsonReadArchive readEquipment(savedEquipment);
    ASSERT_TRUE(equipment->LoadIntoWorld(readEquipment, Hero, Entities(), context));
    ASSERT_EQ(State().Layout, layout);
    Step();
    Step();

    EXPECT_EQ(EffectsOn(), effectsBefore);
    EXPECT_EQ(std::as_const(Entities()).TryGet<AbilityGrants>(Hero)->Count, grantsBefore);
    const GameplayTagContainer& after = *std::as_const(Entities()).TryGet<GameplayTagContainer>(Hero);
    ASSERT_EQ(after.Count, before.Count);
    for (std::uint8_t i = 0; i < before.Count; ++i)
    {
        EXPECT_EQ(after.Tags[i], before.Tags[i]);
        EXPECT_EQ(after.StackCount(before.Tags[i]), before.StackCount(before.Tags[i])) << "nothing held twice";
    }
}

// Skyrim: worn Sprint Boots set the wearer's speed, and a Frozen effect's higher
// precedence wins over them whichever came first; taking the boots off leaves
// Frozen alone.
TEST_F(EquipmentGateTest, FrozenBeatsSprintBootsByPrecedence)
{
    World& world = Entities();
    const AttributeId speed = world.GetResource<AttributeRegistry>().RegisterAttribute("Speed", 0.0f, 100.0f, 5.0f);
    (void)world.TryGet<AttributeSet>(Hero)->Add(speed, 5.0f);
    EffectDefinition sprint{};
    sprint.Duration = EffectDuration::Infinite;
    sprint.Modifiers = { { speed, ModifierOp::Override, 8.0f, 10 } };
    EffectDefinition frozen{};
    frozen.Duration = EffectDuration::Infinite;
    frozen.Modifiers = { { speed, ModifierOp::Override, 0.0f, 100 } };
    EffectRegistry& effects = world.GetResource<EffectRegistry>();
    const EffectId sprinting = effects.Define("asset://effects/sprint.sdata", sprint);
    const EffectId freezing = effects.Define("asset://effects/frozen.sdata", frozen);
    const ItemId boots = world.GetResource<ItemRegistry>().Define(
        "asset://items/sprint_boots.sdata",
        ItemDefinition{ .Name = "Sprint Boots", .Tags = { Weapon }, .Occupies = { "hand.off" },
                        .EquipEffects = { { sprinting, 1.0f } } });
    const auto current = [&] {
        ResolveAttributesWithEffects(world);
        return std::as_const(world).TryGet<AttributeSet>(Hero)->GetCurrent(speed);
    };

    const EntityId worn = Give(boots);
    ASSERT_EQ(Gear().Equip(Hero, worn).Refusal, EquipmentRefusal::None);
    EXPECT_FLOAT_EQ(current(), 8.0f);
    const EntityId ice = ApplyEffect(world, Hero, freezing, FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = Now });
    EXPECT_FLOAT_EQ(current(), 0.0f) << "Frozen outranks the boots";
    ASSERT_EQ(Gear().Unequip(Hero, "hand.off").Refusal, EquipmentRefusal::None);
    EXPECT_FLOAT_EQ(current(), 0.0f);
    ASSERT_TRUE(RemoveEffect(world, ice));
    EXPECT_FLOAT_EQ(current(), 5.0f) << "neither holds it now";
}

// One check per resource (docs/gameplay/abilitykit.md, "Costs"): every row that
// reaches an entry in one commit is checked as one spend, whichever provider
// names it, and a refused commit spends nothing.
namespace
{
    class EquipmentCostTest : public EquipmentGateTest
    {
    protected:
        // An ability paying `costs` (a "costs" list) in one commit as it starts,
        // granted by an item worn in use.x that stacks to 5.
        void Define(std::string_view costs)
        {
            World& world = Entities();
            const std::string json = std::string(R"({ "costs": )") + std::string(costs) + R"(,
                "phases": [ { "key": "p_go", "then": [ { "key": "t_end", "to": "end" } ] } ] })";
            Paying = DefineAbility("asset://abilities/paying.sdata", json);
            EffectDefinition lasting{};
            lasting.Duration = EffectDuration::Infinite;
            const EffectId granting = world.GetResource<EffectRegistry>().Define("asset://effects/paying.sdata", lasting);
            (void)DeclareEffectGrants(world, granting,
                                      { { .Key = "g_pay", .Ability = "asset://abilities/paying.sdata", .Override = {} } });
            Charge = world.GetResource<ItemRegistry>().Define(
                "asset://items/charge.sdata", ItemDefinition{ .Name = "Charge", .Tags = { Usable }, .StackMax = 5,
                                                              .Occupies = { "use.x" }, .EquipEffects = { { granting, 1.0f } } });
            Declare();
        }

        EntityId WearCharges(std::uint32_t count)
        {
            const EntityId entry = Inventory(Entities()).Give(Hero, Charge, count).Entry;
            EXPECT_EQ(Gear().Equip(Hero, entry).Refusal, EquipmentRefusal::None);
            Step();
            return entry;
        }

        // A spent entry is reaped once nothing runs from it: none left.
        std::uint32_t CountOf(EntityId entry)
        {
            return Entities().IsAlive(entry) ? std::as_const(Entities()).TryGet<ItemEntry>(entry)->Count : 0u;
        }

        bool Activates()
        {
            const std::size_t refused = Traced(AbilityTraceEvent::Refused).size();
            Request(Paying);
            Step();
            return Traced(AbilityTraceEvent::Refused).size() == refused;
        }

        AbilityId Paying;
        ItemId Charge;
    };

    constexpr std::string_view kItemAndSlot = R"([ { "key": "g_both", "when": "start", "resources": [
        { "key": "r_item", "provider": "inventory.item", "arguments": { "entry": { "port": "source" } } },
        { "key": "r_slot", "provider": "equipment.slot", "arguments": { "slot": { "const": "use.x" } } } ] } ])";
}

TEST_F(EquipmentCostTest, AnItemRowAndASlotRowOnOneEntryAreOneSpend)
{
    Define(kItemAndSlot);
    const EntityId one = WearCharges(1);
    EXPECT_FALSE(Activates()) << "two units asked of an entry holding one";
    EXPECT_EQ(CountOf(one), 1u);
}

TEST_F(EquipmentCostTest, AnItemRowAndASlotRowOnOneEntryPayBothWhenItHoldsBoth)
{
    Define(kItemAndSlot);
    const EntityId two = WearCharges(2);
    EXPECT_TRUE(Activates());
    EXPECT_EQ(CountOf(two), 0u);
}

TEST_F(EquipmentCostTest, RowsOnDifferentEntriesEachPay)
{
    Define(R"([ { "key": "g_both", "when": "start", "resources": [
        { "key": "r_item", "provider": "inventory.item", "arguments": { "entry": { "port": "source" } } },
        { "key": "r_slot", "provider": "equipment.slot", "arguments": { "slot": { "const": "hand.main" } } } ] } ])");
    const EntityId charges = WearCharges(2);
    const EntityId pistol = Give(Pistol);
    ASSERT_EQ(Gear().Equip(Hero, pistol).Refusal, EquipmentRefusal::None);
    Step();
    EXPECT_TRUE(Activates());
    EXPECT_EQ(CountOf(charges), 1u);
    EXPECT_EQ(CountOf(pistol), 0u);
}

TEST_F(EquipmentCostTest, TwoSlotRowsOnOneSlotAreSummed)
{
    Define(R"([ { "key": "g_twice", "when": "start", "resources": [
        { "key": "r_a", "provider": "equipment.slot", "arguments": { "slot": { "const": "use.x" } } },
        { "key": "r_b", "provider": "equipment.slot", "arguments": { "slot": { "const": "use.x" } } } ] } ])");
    const EntityId one = WearCharges(1);
    EXPECT_FALSE(Activates());
    EXPECT_EQ(CountOf(one), 1u);
}

TEST_F(EquipmentCostTest, ARefusedGroupLeavesEveryGroupsResourcesAsTheyWere)
{
    Define(R"([ { "key": "g_held", "when": "start", "resources": [
                    { "key": "r_x", "provider": "equipment.slot", "arguments": { "slot": { "const": "use.x" } } } ] },
                { "key": "g_empty", "when": "start", "resources": [
                    { "key": "r_ring", "provider": "equipment.slot", "arguments": { "slot": { "const": "ring.left" } } } ] } ])");
    const EntityId charges = WearCharges(3);
    EXPECT_FALSE(Activates()) << "nothing is worn in ring.left";
    EXPECT_EQ(CountOf(charges), 3u) << "use.x paid nothing";
}

TEST(EquipmentCostProvider, AProviderOwnsItsResourceOrSpendsThroughAnotherNeverBoth)
{
    const auto errors = [](AbilityCostProviderDefinition definition) {
        std::vector<std::string> found;
        AbilityCostProviderCatalogTraits::Validate(definition, found);
        return found.size();
    };
    AbilityCostProviderDefinition owns;
    owns.Name = "owns";
    owns.Check = [](const World&, std::span<const AbilityCostRow>) { return AbilityCostProviderCheck{}; };
    owns.Apply = [](World&, std::span<const AbilityCostRow>) {};
    EXPECT_EQ(errors(owns), 0u);
    AbilityCostProviderDefinition through;
    through.Name = "through";
    through.SpendsThrough = "owns";
    through.Translate = [](const World&, const AbilityCostRow&, std::vector<AuthoredValue>&) { return true; };
    EXPECT_EQ(errors(through), 0u);
    AbilityCostProviderDefinition both = owns;
    both.SpendsThrough = "other";
    both.Translate = through.Translate;
    EXPECT_GT(errors(both), 0u);
    AbilityCostProviderDefinition half;
    half.Name = "half";
    half.Check = owns.Check;
    EXPECT_GT(errors(half), 0u);
    AbilityCostProviderDefinition itself = through;
    itself.Name = "owns";
    EXPECT_GT(errors(itself), 0u);
}

// A translation leading to another translation has no batch to join.
TEST_F(EquipmentCostTest, ASpendThroughATranslatingProviderIsRefused)
{
    AbilityCostProviderDefinition alias;
    alias.Name = "test.alias";
    alias.Arguments.Children = { MakeDataField(DataFieldKind::String, "slot", "Slot") };
    alias.SpendsThrough = std::string(kEquipmentSlotCostProvider);
    alias.Translate = [](const World&, const AbilityCostRow& row, std::vector<AuthoredValue>& arguments) {
        arguments.assign(row.Arguments.begin(), row.Arguments.end());
        return true;
    };
    AbilityCostProviderRegistrationScope scope(Entities().GetResource<AbilityCostProviderCatalog>(), "test");
    ASSERT_TRUE(scope.Declare(std::move(alias)));
    ASSERT_TRUE(scope.Commit());
    Define(R"([ { "key": "g_alias", "when": "start", "resources": [
        { "key": "r_a", "provider": "test.alias", "arguments": { "slot": { "const": "use.x" } } } ] } ])");
    const EntityId charges = WearCharges(2);
    EXPECT_FALSE(Activates());
    EXPECT_EQ(CountOf(charges), 2u);
}

// Worn grant entries are content: whatever order a machine declares items in,
// it interns the same entries under the same wire keys.
namespace
{
    std::vector<std::uint32_t> DeclaredWornKeys(bool reversed)
    {
        World world;
        world.RegisterComponent<PersistentIdComponent>();
        RegisterAbilityKit(world);
        InstallInventoryVocabulary(world);
        InstallEquipmentVocabulary(world);
        const GameplayTagId ring = *world.GetResource<GameplayTagRegistry>().RegisterTag("Item.Ring");
        EffectDefinition lasting{};
        lasting.Duration = EffectDuration::Infinite;
        const EffectId glow = world.GetResource<EffectRegistry>().Define("asset://effects/glow.sdata", lasting);
        (void)DeclareEffectGrants(world, glow, { { .Key = "g_bolt", .Ability = "asset://abilities/bolt.sdata", .Override = {} } });
        EquipmentLayout layout;
        for (const auto& [key, priority] : { std::pair{ "ring.left", 20 }, std::pair{ "ring.right", 21 } })
        {
            EquipmentSlot slot;
            slot.Key = key;
            slot.Accepts = ring;
            slot.Binding.Priority = priority;
            layout.Slots.push_back(std::move(slot));
        }
        (void)world.GetResource<EquipmentLayoutRegistry>().Define("asset://equipment/hands.sdata", std::move(layout));
        std::vector<std::pair<std::string, double>> rings{ { "asset://items/ember.sdata", 7.0 },
                                                           { "asset://items/frost.sdata", 9.0 } };
        if (reversed)
            std::ranges::reverse(rings);
        for (const auto& [path, power] : rings)
            (void)world.GetResource<ItemRegistry>().Define(
                path, ItemDefinition{ .Name = path, .Tags = { ring }, .Occupies = { "ring" },
                                      .EquipEffects = { { glow, 1.0f } },
                                      .GrantParameters = { { .Key = "g_bolt", .Parameters = { { .Key = "power", .Value = power } } } } });
        std::vector<std::string> errors;
        DeclareWornGrantEntries(world, errors);
        EXPECT_TRUE(errors.empty());
        const AbilityGrantEntries& entries = world.GetResource<AbilityGrantEntries>();
        std::vector<std::uint32_t> keys;
        for (std::size_t i = 0; i < entries.Size(); ++i)
            keys.push_back(entries.WireKey(static_cast<std::uint16_t>(i)));
        std::ranges::sort(keys);
        return keys;
    }
}

TEST(EquipmentWornEntries, EveryOrderOfDeclaringContentInternsTheSameEntries)
{
    const std::vector<std::uint32_t> forward = DeclaredWornKeys(false);
    EXPECT_EQ(forward.size(), 5u) << "the effect's own entry, and two rings in two slots";
    EXPECT_EQ(forward, DeclaredWornKeys(true));
}

TEST_F(EquipmentTest, AWornEntryTheTableRefusesIsReportedWithItsItemAndSlot)
{
    EffectDefinition lasting{};
    lasting.Duration = EffectDuration::Infinite;
    const EffectId spark = Entities().GetResource<EffectRegistry>().Define("asset://effects/spark.sdata", lasting);
    (void)DeclareEffectGrants(Entities(), spark, { { .Key = "g_spark", .Ability = "asset://abilities/bolt.sdata", .Override = {} } });
    AbilityGrantEntries& entries = Entities().GetResource<AbilityGrantEntries>();
    for (std::size_t i = entries.Size(); i < kAbilityNoGrantEntry; ++i)
        ASSERT_NE(entries.Intern({ "fill." + std::to_string(i), Slash, {} }), kAbilityNoGrantEntry);
    (void)Entities().GetResource<ItemRegistry>().Define(
        "asset://items/spark_ring.sdata",
        ItemDefinition{ .Name = "Spark", .Tags = { Ring }, .Occupies = { "ring" }, .EquipEffects = { { spark, 1.0f } } });
    std::vector<std::string> errors;
    DeclareWornGrantEntries(Entities(), errors);
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors.front().find("asset://items/spark_ring.sdata worn in 'ring.left'"), std::string::npos)
        << errors.front();
}

// Derivation (docs/gameplay/equipment.md, "Derivation"): a record is current with
// every definition it reads, changes whole, and a refusal is retried as its kind
// allows.
TEST_F(EquipmentTest, AReloadedEffectsGrantListReachesAWornInstance)
{
    const EntityId ring = Give(RingItem);
    ASSERT_EQ(Gear().Equip(Hero, ring).Refusal, EquipmentRefusal::None);
    Step();
    ASSERT_EQ(GrantsOf(Bolt).size(), 1u);
    (void)DeclareEffectGrants(Entities(), Glow, { { .Key = "g_slash", .Ability = "asset://abilities/slash.sdata", .Override = {} } });
    for (int i = 0; i < 3; ++i)
        Step();
    EXPECT_EQ(GrantsOf(Bolt).size(), 0u) << "the reloaded effect no longer grants bolt";
    EXPECT_EQ(GrantsOf(Slash).size(), 1u);
}

TEST_F(EquipmentTest, AReloadedEffectsTagsFollowTheWearerWithNothingLeftOrDoubled)
{
    World& world = Entities();
    GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
    const GameplayTagId shining = *tags.RegisterTag("State.Shining");
    const GameplayTagId glowing = *tags.RegisterTag("State.Glowing");
    EffectDefinition shine{};
    shine.Duration = EffectDuration::Infinite;
    shine.GrantedTags = { shining };
    const EffectId effect = world.GetResource<EffectRegistry>().Define("asset://effects/shine.sdata", shine);
    const ItemId item = world.GetResource<ItemRegistry>().Define(
        "asset://items/shine_ring.sdata",
        ItemDefinition{ .Name = "Shine", .Tags = { Ring }, .Occupies = { "ring" }, .EquipEffects = { { effect, 1.0f } } });
    ASSERT_EQ(Gear().Equip(Hero, Give(item)).Refusal, EquipmentRefusal::None);
    Step();
    ASSERT_EQ(std::as_const(world).TryGet<GameplayTagContainer>(Hero)->StackCount(shining), 1u);
    shine.GrantedTags = { glowing };
    (void)world.GetResource<EffectRegistry>().Define("asset://effects/shine.sdata", shine);
    Step();
    Step();
    const GameplayTagContainer& held = *std::as_const(world).TryGet<GameplayTagContainer>(Hero);
    EXPECT_FALSE(held.HasExact(shining));
    EXPECT_EQ(held.StackCount(glowing), 1u);
    EXPECT_EQ(EffectsOn(), 1u);
}

// Modifiers fold from the definition as it is, so a reload reaches them as is.
TEST_F(EquipmentTest, AReloadedEffectsModifierFollowsTheWearer)
{
    World& world = Entities();
    const AttributeId speed = world.GetResource<AttributeRegistry>().RegisterAttribute("Speed", 0.0f, 100.0f, 5.0f);
    world.AddComponent<AttributeSet>(Hero);
    (void)world.TryGet<AttributeSet>(Hero)->Add(speed, 5.0f);
    EffectDefinition fast{};
    fast.Duration = EffectDuration::Infinite;
    fast.Modifiers = { { speed, ModifierOp::Add, 2.0f } };
    const EffectId effect = world.GetResource<EffectRegistry>().Define("asset://effects/fast.sdata", fast);
    const ItemId item = world.GetResource<ItemRegistry>().Define(
        "asset://items/fast_ring.sdata",
        ItemDefinition{ .Name = "Fast", .Tags = { Ring }, .Occupies = { "ring" }, .EquipEffects = { { effect, 1.0f } } });
    ASSERT_EQ(Gear().Equip(Hero, Give(item)).Refusal, EquipmentRefusal::None);
    const auto current = [&] {
        ResolveAttributesWithEffects(world);
        return std::as_const(world).TryGet<AttributeSet>(Hero)->GetCurrent(speed);
    };
    EXPECT_FLOAT_EQ(current(), 7.0f);
    fast.Modifiers = { { speed, ModifierOp::Add, 5.0f } };
    (void)world.GetResource<EffectRegistry>().Define("asset://effects/fast.sdata", fast);
    Step();
    EXPECT_FLOAT_EQ(current(), 10.0f);
    EXPECT_EQ(EffectsOn(), 1u);
}

TEST_F(EquipmentTest, AnEquipEffectRedefinedInstantIsRefusedAndMaintainsNothingUntilItIsInfiniteAgain)
{
    World& world = Entities();
    const AttributeId power = world.GetResource<AttributeRegistry>().RegisterAttribute("Power", 0.0f, 100.0f, 0.0f);
    world.AddComponent<AttributeSet>(Hero);
    (void)world.TryGet<AttributeSet>(Hero)->Add(power, 1.0f);
    ASSERT_EQ(Gear().Equip(Hero, Give(RingItem)).Refusal, EquipmentRefusal::None);
    Step();
    EffectDefinition instant{};
    instant.Duration = EffectDuration::Instant;
    instant.Modifiers = { { power, ModifierOp::Add, 10.0f } };
    (void)world.GetResource<EffectRegistry>().Define("asset://effects/glow.sdata", instant);
    Step();
    ASSERT_EQ(State().Count, 1u) << "still worn";
    EXPECT_EQ(State().Records[0].Derivation, EquipmentDerivation::RefusedByContent);
    EXPECT_EQ(EffectsOn(), 0u) << "inactive";
    EXPECT_FLOAT_EQ(std::as_const(world).TryGet<AttributeSet>(Hero)->GetBase(power), 1.0f) << "never applied as Instant";
    const EquipmentState before = State();
    EXPECT_EQ(Gear().Equip(Hero, Give(RingItem)).Refusal, EquipmentRefusal::NotMaintained);
    EXPECT_EQ(std::memcmp(&before, &State(), sizeof(EquipmentState)), 0);

    EffectDefinition lasting{};
    lasting.Duration = EffectDuration::Infinite;
    (void)world.GetResource<EffectRegistry>().Define("asset://effects/glow.sdata", lasting);
    Step();
    EXPECT_EQ(State().Records[0].Derivation, EquipmentDerivation::Maintained);
    EXPECT_EQ(EffectsOn(), 1u);
}

namespace
{
    // The body layout with its slots in another order, or without ring.left, or
    // with ring.left at another priority.
    std::string BodyLayout(std::string_view leftRing, bool reversed)
    {
        const std::string left = std::string(leftRing);
        const std::string right =
            R"({ "key": "ring.right", "accepts": "Item.Ring", "action": "Input.AltFire", "priority": 21 })";
        std::string rings = left.empty() ? right : reversed ? right + ", " + left : left + ", " + right;
        return std::string(R"({ "slots": [ { "key": "hand.main", "accepts": "Item.Weapon", "action": "Input.PrimaryFire", "priority": 10 },
            { "key": "hand.off", "accepts": "Item.Weapon" },
            { "key": "use.x", "accepts": "Item.Usable", "on_depleted": "hold" }, )") + rings
            + R"( ], "lock_tags": [ "State.Attacking" ] })";
    }

    constexpr std::string_view kLeftRing =
        R"({ "key": "ring.left", "accepts": "Item.Ring", "action": "Input.AltFire", "priority": 20 })";
}

TEST_F(EquipmentTest, ALayoutReloadKeepsALiveRecordOnTheSlotsItHeldByKey)
{
    const EntityId ring = Give(RingItem);
    ASSERT_EQ(Gear().Equip(Hero, ring, "ring.left").Refusal, EquipmentRefusal::None);
    Step();
    EquipmentLayout layout;
    std::vector<std::string> errors;
    ASSERT_TRUE(ResolveEquipmentLayoutDeclaration(*Compile(BodyLayout(kLeftRing, true)), Entities(), layout, errors));
    (void)RedefineEquipmentLayout(Entities(), "asset://equipment/body.sdata", std::move(layout));
    Step();
    const EquipmentLayout& now = *Entities().GetResource<EquipmentLayoutRegistry>().Get(Body);
    ASSERT_EQ(State().Count, 1u);
    EXPECT_EQ(now.Slots[State().Records[0].Primary].Key, "ring.left");
    EXPECT_EQ(EffectsOn(), 1u);
}

TEST_F(EquipmentTest, ALayoutReloadThatRemovesASlotEndsTheRecordItHeldAndTheSceneStillSaves)
{
    ASSERT_EQ(Gear().Equip(Hero, Give(RingItem), "ring.left").Refusal, EquipmentRefusal::None);
    ASSERT_EQ(Gear().Equip(Hero, Give(Greatsword)).Refusal, EquipmentRefusal::None);
    Step();
    EquipmentLayout layout;
    std::vector<std::string> errors;
    ASSERT_TRUE(ResolveEquipmentLayoutDeclaration(*Compile(BodyLayout({}, false)), Entities(), layout, errors));
    (void)RedefineEquipmentLayout(Entities(), "asset://equipment/body.sdata", std::move(layout));
    Step();
    ASSERT_EQ(State().Count, 1u) << "the ring's slot went; the sword's stayed";
    EXPECT_EQ(State().Records[0].Definition, Greatsword);
    EXPECT_EQ(EffectsOn(), 1u);
    LoggingProvider logging;
    SceneSerializationContext context{ logging };
    JsonWriteArchive written;
    EXPECT_TRUE(MakeEquipmentStateSerializer()->Save(written, Hero, Entities(), context));
}

TEST_F(EquipmentTest, ALayoutReloadThatMovesASlotsPriorityWearsTheGrantAtTheNewOne)
{
    ASSERT_EQ(Gear().Equip(Hero, Give(RingItem), "ring.left").Refusal, EquipmentRefusal::None);
    Step();
    EquipmentLayout layout;
    std::vector<std::string> errors;
    ASSERT_TRUE(ResolveEquipmentLayoutDeclaration(
        *Compile(BodyLayout(R"({ "key": "ring.left", "accepts": "Item.Ring", "action": "Input.AltFire", "priority": 30 })", false)),
        Entities(), layout, errors));
    (void)RedefineEquipmentLayout(Entities(), "asset://equipment/body.sdata", std::move(layout));
    Step();
    Step();
    ASSERT_EQ(GrantsOf(Bolt).size(), 1u);
    EXPECT_EQ(Entities().GetResource<AbilityGrantEntries>().Find(GrantsOf(Bolt)[0]->Entry)->Override.Priority, 30);
}

TEST_F(EquipmentTest, ARecordRefusedForRoomIsTracedOnceAndDerivedOnceThereIsRoom)
{
    World& world = Entities();
    AbilityGrants& filling = *world.TryGet<AbilityGrants>(Hero);
    const std::uint16_t filler = world.GetResource<AbilityGrantEntries>().Intern({ "g_fill", Slash, {} });
    while (filling.Count < kAbilityGrantCapacity)
        (void)GrantAbility(filling, Slash, AbilityGrantKind::Authored, filler);
    const EntityId ring = Give(RingItem);
    EquipmentState& state = *world.TryGet<EquipmentState>(Hero);
    state.Records[0] = EquipmentRecord{ .Key = 1, .Definition = RingItem, .Slots = 1u << 2, .Primary = 2,
                                        .Source = MakePersistableRef(world, ring),
                                        .Derivation = EquipmentDerivation::Pending };
    state.Count = 1;
    state.NextKey = 2;
    const auto refusals = [&] {
        std::size_t count = 0;
        world.GetResource<AbilityTrace>().Records().ForEach([&](const AbilityTraceRecord& record) {
            count += record.Event == AbilityTraceEvent::GrantRefused && record.Actor == Hero ? 1 : 0;
        });
        return count;
    };
    Pass();
    Pass();
    EXPECT_EQ(State().Records[0].Derivation, EquipmentDerivation::RefusedForRoom);
    EXPECT_EQ(EffectsOn(), 0u);
    EXPECT_EQ(refusals(), 1u) << "once, as it became refused";
    *world.TryGet<AbilityGrants>(Hero) = AbilityGrants{};
    Pass();
    EXPECT_EQ(State().Records[0].Derivation, EquipmentDerivation::Maintained);
    EXPECT_EQ(EffectsOn(), 1u);
}

// A reload whose new derivation does not fit keeps the old effects exactly:
// never some of each.
TEST_F(EquipmentTest, AReloadThatDoesNotFitLeavesTheOldEffectsAsTheyWere)
{
    World& world = Entities();
    ASSERT_EQ(Gear().Equip(Hero, Give(RingItem)).Refusal, EquipmentRefusal::None);
    Step();
    AbilityGrants& filling = *world.TryGet<AbilityGrants>(Hero);
    const std::uint16_t filler = world.GetResource<AbilityGrantEntries>().Intern({ "g_fill", Slash, {} });
    while (filling.Count < kAbilityGrantCapacity)
        (void)GrantAbility(filling, Slash, AbilityGrantKind::Authored, filler);
    (void)DeclareEffectGrants(world, Glow, { { .Key = "g_bolt", .Ability = "asset://abilities/bolt.sdata", .Override = {} },
                                             { .Key = "g_more", .Ability = "asset://abilities/slash.sdata", .Override = {} } });
    Step();
    Step();
    EXPECT_EQ(State().Records[0].Derivation, EquipmentDerivation::RefusedForRoom);
    EXPECT_EQ(EffectsOn(), 1u);
    EXPECT_EQ(GrantsOf(Bolt).size(), 1u) << "the old derivation's grant, as it was";
}

// Rebinding and cycling walk everything a wearer holds, however deep; a
// waiting record is rescanned only when what its wearer holds, or its
// records, changed.
namespace
{
    class EquipmentWalkTest : public EquipmentTest
    {
    protected:
        EquipmentWalkTest()
        {
            World& world = Entities();
            EquipmentLayout belt;
            for (const char* key : { "use.x", "use.y" })
            {
                EquipmentSlot slot;
                slot.Key = key;
                slot.Accepts = Usable;
                slot.OnDepleted = SlotDepletion::Hold;
                belt.Slots.push_back(std::move(slot));
            }
            Belt = world.GetResource<EquipmentLayoutRegistry>().Define("asset://equipment/belt.sdata", std::move(belt));
            Charge = world.GetResource<ItemRegistry>().Define(
                "asset://items/charge.sdata",
                ItemDefinition{ .Name = "Charge", .Tags = { Usable }, .StackMax = 5, .Occupies = { "use" } });
            Declare();
        }

        // A wearer with a container and the belt layout.
        EntityId Wearer()
        {
            World& world = Entities();
            const EntityId wearer = world.CreateEntity();
            world.AddComponent<ItemContainer>(wearer, ItemContainer{});
            world.AddComponent<GameplayTagContainer>(wearer, GameplayTagContainer{});
            world.AddComponent<AbilityGrants>(wearer, AbilityGrants{});
            world.AddComponent<EquipmentState>(wearer, EquipmentState{ .Layout = Belt, .Count = 0, .NextKey = 1, .Records = {} });
            return wearer;
        }

        // A bag in `holder`'s container.
        EntityId BagIn(EntityId holder)
        {
            const EntityId bag = Inventory(Entities()).Give(holder, Pebble, 1).Entry;
            Entities().AddComponent<ItemContainer>(bag, ItemContainer{});
            return bag;
        }

        // `wearer`'s record in `slot` left waiting: its bomb spent.
        void LeaveWaiting(EntityId wearer, std::string_view slot)
        {
            const EntityId bomb = Inventory(Entities()).Give(wearer, Charge, 1).Entry;
            ASSERT_EQ(Gear().Equip(wearer, bomb, slot).Refusal, EquipmentRefusal::None);
            Entities().TryGet<ItemEntry>(bomb)->Count = 0;
            NoteDepleted(Entities(), bomb, EntityId{});
            Pass();
        }

        EntityId SourceIn(EntityId wearer, std::string_view slot)
        {
            const EquipmentState& state = *std::as_const(Entities()).TryGet<EquipmentState>(wearer);
            const std::uint8_t index =
                FindEquipmentSlot(*Entities().GetResource<EquipmentLayoutRegistry>().Get(state.Layout), slot);
            for (std::size_t i = 0; i < state.Count; ++i)
                if ((state.Records[i].Slots >> index) & 1u)
                    return ResolvePersistableRef(Entities(), state.Records[i].Source);
            return {};
        }

        EquipmentLayoutId Belt;
        ItemId Charge;
    };
}

TEST_F(EquipmentWalkTest, AWaitingRecordTakesAnEntryInABag)
{
    const EntityId wearer = Wearer();
    LeaveWaiting(wearer, "use.x");
    ASSERT_FALSE(SourceIn(wearer, "use.x").IsValid());
    const EntityId bagged = Inventory(Entities()).Give(BagIn(wearer), Charge, 3).Entry;
    Pass();
    EXPECT_EQ(SourceIn(wearer, "use.x"), bagged);
}

TEST_F(EquipmentWalkTest, AnEntryMovedIntoOneHoldersBagRebindsThatHolder)
{
    const EntityId waiting = Wearer();
    const EntityId other = Wearer();
    LeaveWaiting(waiting, "use.x");
    LeaveWaiting(other, "use.x");
    const EntityId theirs = Inventory(Entities()).Give(BagIn(other), Charge, 2).Entry;
    Pass();
    ASSERT_EQ(SourceIn(other, "use.x"), theirs) << "the other's own bag";
    const EntityId spare = Inventory(Entities()).Give(BagIn(other), Charge, 5).Entry;
    Pass();
    ASSERT_EQ(Inventory(Entities()).Transfer(spare, BagIn(waiting), 0).Refusal, InventoryRefusal::None);
    Pass();
    EXPECT_EQ(SourceIn(waiting, "use.x"), spare);
    EXPECT_EQ(SourceIn(other, "use.x"), theirs);
}

// F15: an entry another record lets go is a candidate, with no inventory change.
TEST_F(EquipmentWalkTest, UnequippingAnotherSlotFreesItsStackForAWaitingRecord)
{
    const EntityId wearer = Wearer();
    LeaveWaiting(wearer, "use.x");
    const EntityId stack = Inventory(Entities()).Give(wearer, Charge, 3).Entry;
    ASSERT_EQ(Gear().Equip(wearer, stack, "use.y").Refusal, EquipmentRefusal::None);
    Pass();
    ASSERT_FALSE(SourceIn(wearer, "use.x").IsValid()) << "use.y wears the only stack";
    ASSERT_EQ(Gear().Unequip(wearer, "use.y").Refusal, EquipmentRefusal::None);
    Pass();
    EXPECT_EQ(SourceIn(wearer, "use.x"), stack);
}

TEST_F(EquipmentWalkTest, AnotherHoldersChangeDoesNotRescanAWaitingRecord)
{
    const EntityId waiting = Wearer();
    const EntityId busy = Wearer();
    LeaveWaiting(waiting, "use.x");
    Pass();
    const std::uint64_t walks = EquipmentRebindWalks(Entities());
    for (int i = 0; i < 5; ++i)
    {
        (void)Inventory(Entities()).Give(busy, Pebble, 1);
        Pass();
    }
    EXPECT_EQ(EquipmentRebindWalks(Entities()), walks) << "nothing the waiting wearer holds changed";
    (void)Inventory(Entities()).Give(waiting, Pebble, 1);
    Pass();
    EXPECT_EQ(EquipmentRebindWalks(Entities()), walks + 1) << "its own inventory did";
}

TEST_F(EquipmentWalkTest, ACountChangeOnATrackedEntryRescansItsHolder)
{
    const EntityId waiting = Wearer();
    LeaveWaiting(waiting, "use.x");
    const EntityId pebbles = Inventory(Entities()).Give(waiting, Pebble, 1).Entry;
    Pass();
    const std::uint64_t walks = EquipmentRebindWalks(Entities());
    NoteHeldRecordChanged(Entities(), pebbles);
    Pass();
    EXPECT_EQ(EquipmentRebindWalks(Entities()), walks + 1);
}

// F10: an item the slot accepts by tag but whose occupies does not name it is
// passed over, not chosen and refused on every press.
TEST_F(EquipmentTest, CyclingPassesOverAnItemWhoseOccupiesDoesNotCoverTheSlot)
{
    const ItemId wand = Entities().GetResource<ItemRegistry>().Define(
        "asset://items/wand.sdata", ItemDefinition{ .Name = "Wand", .Tags = { Weapon, Ring }, .Occupies = { "ring" } });
    Declare();
    (void)Give(wand);
    const EntityId sword = Give(Greatsword);
    ASSERT_EQ(Gear().Cycle(Hero, "hand").Refusal, EquipmentRefusal::None);
    EXPECT_EQ(ResolvePersistableRef(Entities(), State().Records[0].Source), sword);
}

TEST_F(EquipmentTest, CyclingSkipsACandidateTheEquipRefuses)
{
    World& world = Entities();
    AbilityGrants& filling = *world.TryGet<AbilityGrants>(Hero);
    const std::uint16_t filler = world.GetResource<AbilityGrantEntries>().Intern({ "g_fill", Slash, {} });
    while (filling.Count < kAbilityGrantCapacity)
        (void)GrantAbility(filling, Slash, AbilityGrantKind::Authored, filler);
    const ItemId club = world.GetResource<ItemRegistry>().Define(
        "asset://items/club.sdata", ItemDefinition{ .Name = "Club", .Tags = { Weapon }, .Occupies = { "hand.main" } });
    Declare();
    (void)Give(Greatsword);
    const EntityId stick = Give(club);
    ASSERT_EQ(Gear().Cycle(Hero, "hand").Refusal, EquipmentRefusal::None) << "the greatsword's grants do not fit";
    EXPECT_EQ(ResolvePersistableRef(world, State().Records[0].Source), stick);
}

// What a person asks for and what the World decides (docs/gameplay/equipment.md,
// "Locks"): a wearer's lock or a record's own refuses the first; a forced move
// or unequip skips them and nothing else.
namespace
{
    class EquipmentLockTest : public EquipmentTest
    {
    protected:
        EquipmentLockTest()
        {
            World& world = Entities();
            Bound = *world.GetResource<GameplayTagRegistry>().RegisterTag("Curse.Bound");
            EffectDefinition curse{};
            curse.Duration = EffectDuration::Infinite;
            curse.GrantedTags = { Bound };
            Curse = world.GetResource<EffectRegistry>().Define("asset://effects/curse.sdata", curse);
            CursedRing = world.GetResource<ItemRegistry>().Define(
                "asset://items/cursed_ring.sdata",
                ItemDefinition{ .Name = "Cursed Ring", .Tags = { Ring }, .Occupies = { "ring" },
                                .EquipEffects = { { Glow, 1.0f } }, .EquipLockTags = { Bound } });
            Declare();
        }

        // A cursed ring whose curse is authored on the entry itself.
        EntityId AuthoredCurse()
        {
            const EntityId ring = Give(CursedRing);
            (void)Entities().TryGet<GameplayTagContainer>(ring)->GrantBaseline(Bound);
            return ring;
        }

        EntityId Chest()
        {
            const EntityId chest = Entities().CreateEntity();
            Entities().AddComponent<ItemContainer>(chest, ItemContainer{});
            return chest;
        }

        GameplayTagId Bound;
        EffectId Curse;
        ItemId CursedRing;
    };
}

TEST_F(EquipmentLockTest, AForcedTransferMovesAWornEntryUnderTheWearersLock)
{
    const EntityId sword = Give(Greatsword);
    ASSERT_EQ(Gear().Equip(Hero, sword).Refusal, EquipmentRefusal::None);
    Entities().TryGet<GameplayTagContainer>(Hero)->Grant(Attacking);
    const EntityId chest = Chest();
    EXPECT_EQ(Inventory(Entities()).Transfer(sword, chest, 0).Refusal, InventoryRefusal::Withheld);
    ASSERT_EQ(Inventory(Entities()).Transfer(sword, chest, 0, ItemChangeClass::Forced).Refusal, InventoryRefusal::None);
    EXPECT_EQ(State().Count, 0u) << "unequipped in the move";
    EXPECT_EQ(EffectsOn(), 0u);
}

TEST_F(EquipmentLockTest, ForcingSkipsTheLockAndNothingElse)
{
    const EntityId bag = Give(Pebble);
    Entities().AddComponent<ItemContainer>(bag, ItemContainer{});
    EXPECT_EQ(Inventory(Entities()).Transfer(bag, bag, 0, ItemChangeClass::Forced).Refusal, InventoryRefusal::NotAccepted)
        << "into its own contents";
    Entities().AddResource<SimulationAuthority>(SimulationAuthority{ .Authoritative = false });
    EXPECT_EQ(Inventory(Entities()).Transfer(bag, Chest(), 0, ItemChangeClass::Forced).Refusal,
              InventoryRefusal::NotAuthority);
    EXPECT_EQ(Gear().Unequip(Hero, "ring.left", ItemChangeClass::Forced).Refusal, EquipmentRefusal::NotAuthority);
}

// Two cursed rings, each holding its own curse: one authored on its entry, one
// from an effect on its entry. Each locks its own record alone.
TEST_F(EquipmentLockTest, ACurseLocksOnlyTheRecordWhoseEntryHoldsIt)
{
    World& world = Entities();
    const EntityId left = AuthoredCurse();
    ASSERT_EQ(Gear().Equip(Hero, left, "ring.left").Refusal, EquipmentRefusal::None);
    ASSERT_EQ(Gear().Equip(Hero, Give(RingItem), "ring.right").Refusal, EquipmentRefusal::None);
    ASSERT_EQ(Gear().Equip(Hero, Give(RingItem), "ring.right").Refusal, EquipmentRefusal::None)
        << "an uncursed ring swaps freely beside a cursed one";
    EXPECT_EQ(Gear().Unequip(Hero, "ring.left").Refusal, EquipmentRefusal::Locked);
    EXPECT_EQ(Gear().Equip(Hero, Give(RingItem), "ring.left").Refusal, EquipmentRefusal::Locked)
        << "nor is it replaced";

    const EntityId right = Give(CursedRing);
    ASSERT_EQ(Gear().Equip(Hero, right, "ring.right").Refusal, EquipmentRefusal::None);
    const EntityId hex = ApplyEffect(world, right, Curse, FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = Now });
    ASSERT_TRUE(hex.IsValid());
    EXPECT_EQ(Gear().Unequip(Hero, "ring.right").Refusal, EquipmentRefusal::Locked);
    ASSERT_TRUE(RemoveEffect(world, hex));
    EXPECT_EQ(Gear().Unequip(Hero, "ring.right").Refusal, EquipmentRefusal::None) << "its curse lifted";
    EXPECT_EQ(Gear().Unequip(Hero, "ring.left").Refusal, EquipmentRefusal::Locked) << "the other's is not";
    const EntityId chest = Chest();
    EXPECT_EQ(Inventory(world).Drop(left, Transform3f::Identity(), StoragePartitionId::Default()).Refusal,
              InventoryRefusal::Withheld)
        << "a cursed ring is not dropped either";
    EXPECT_EQ(Gear().Unequip(Hero, "ring.left", ItemChangeClass::Forced).Refusal, EquipmentRefusal::None);
    EXPECT_EQ(State().Count, 0u);
    (void)chest;
}

TEST_F(EquipmentLockTest, InstanceTagsSeedTheCurseOfARingGiveMakes)
{
    World& world = Entities();
    const ItemId seeded = world.GetResource<ItemRegistry>().Define(
        "asset://items/seeded_ring.sdata",
        ItemDefinition{ .Name = "Seeded", .Tags = { Ring }, .InstanceTags = { Bound }, .Occupies = { "ring" },
                        .EquipEffects = { { Glow, 1.0f } }, .EquipLockTags = { Bound } });
    Declare();
    const EntityId ring = Give(seeded);
    ASSERT_NE(std::as_const(world).TryGet<GameplayTagContainer>(ring), nullptr);
    EXPECT_EQ(std::as_const(world).TryGet<GameplayTagContainer>(ring)->BaselineCount(Bound), 1u) << "its own, persisted";
    ASSERT_EQ(Gear().Equip(Hero, ring).Refusal, EquipmentRefusal::None);
    EXPECT_EQ(Gear().Unequip(Hero, "ring.left").Refusal, EquipmentRefusal::Locked);
}

TEST(EquipmentLockData, InstanceTagsOnAStackAreRefused)
{
    World world;
    GameplayTagRegistry& tags = world.AddResource<GameplayTagRegistry>();
    (void)tags.RegisterTag("Curse.Bound");
    ItemDeclaration declaration;
    declaration.Name = "Cursed Bombs";
    declaration.StackMax = 5;
    declaration.InstanceTags = { "Curse.Bound" };
    ItemDefinition definition;
    std::vector<std::string> errors;
    EXPECT_FALSE(ResolveItemDeclaration(declaration, world, definition, errors));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors.front().find("instance_tags"), std::string::npos) << errors.front();
}

TEST_F(EquipmentVocabularyTest, AForcedUnequipTakesOffACursedRing)
{
    World& world = Entities();
    const GameplayTagId bound = *world.GetResource<GameplayTagRegistry>().RegisterTag("Curse.Bound");
    const ItemId cursed = world.GetResource<ItemRegistry>().Define(
        "asset://items/cursed_ring.sdata",
        ItemDefinition{ .Name = "Cursed", .Tags = { Ring }, .InstanceTags = { bound }, .Occupies = { "ring" },
                        .EquipEffects = { { Glow, 1.0f } }, .EquipLockTags = { bound } });
    Declare();
    ASSERT_EQ(Gear().Equip(Hero, Give(cursed)).Refusal, EquipmentRefusal::None);
    EXPECT_EQ(Invoke(kEquipmentUnequipVerb, { { "wearer", Hero } }, { { "slot", "ring.left" } }), VerbAdmission::Refused);
    EXPECT_EQ(InvokeForced(kEquipmentUnequipVerb, Hero, "ring.left"), VerbAdmission::Accepted);
    EXPECT_EQ(State().Count, 0u);
}

// §B.4 against a fresh World: the wearer, its container, its entries and its
// equipment saved through the scene serializer and loaded as new entities. Two
// rings grant overlapping tags and add to one attribute; reconciling again
// changes nothing, and taking one off takes exactly its own contributions.
namespace
{
    struct ShineRings
    {
        GameplayTagId Shining, Warded;
        AttributeId Power;
        ItemId Sun, Moon;
    };

    ShineRings DefineShineRings(World& world)
    {
        ShineRings rings;
        GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
        rings.Shining = *tags.RegisterTag("State.Shining");
        rings.Warded = *tags.RegisterTag("State.Warded");
        rings.Power = world.GetResource<AttributeRegistry>().RegisterAttribute("Power", 0.0f, 100.0f, 0.0f);
        EffectRegistry& effects = world.GetResource<EffectRegistry>();
        EffectDefinition sun{};
        sun.Duration = EffectDuration::Infinite;
        sun.GrantedTags = { rings.Shining, rings.Warded };
        sun.Modifiers = { { rings.Power, ModifierOp::Add, 2.0f } };
        const EffectId sunEffect = effects.Define("asset://effects/sun.sdata", sun);
        (void)DeclareEffectGrants(world, sunEffect, { { .Key = "g_ward", .Ability = "asset://abilities/ward.sdata", .Override = {} } });
        EffectDefinition moon{};
        moon.Duration = EffectDuration::Infinite;
        moon.GrantedTags = { rings.Shining };
        moon.Modifiers = { { rings.Power, ModifierOp::Add, 3.0f } };
        const EffectId moonEffect = effects.Define("asset://effects/moon.sdata", moon);
        const GameplayTagId ring = tags.FindTag("Item.Ring");
        ItemRegistry& items = world.GetResource<ItemRegistry>();
        rings.Sun = items.Define("asset://items/sun_ring.sdata", ItemDefinition{ .Name = "Sun", .Tags = { ring },
                                                                                 .Occupies = { "ring" },
                                                                                 .EquipEffects = { { sunEffect, 1.0f } } });
        rings.Moon = items.Define("asset://items/moon_ring.sdata", ItemDefinition{ .Name = "Moon", .Tags = { ring },
                                                                                   .Occupies = { "ring" },
                                                                                   .EquipEffects = { { moonEffect, 1.0f } } });
        return rings;
    }

    // Every contributed stack on `wearer` is accounted for by a live effect's
    // own record of what it granted.
    void ExpectContributionsAttributable(const World& world, EntityId wearer)
    {
        std::vector<std::pair<GameplayTagId, std::uint16_t>> granted;
        world.ForEachComponent<ActiveEffect>([&](EntityId, const ActiveEffect& effect) {
            if (effect.Target != wearer)
                return;
            for (std::uint8_t i = 0; i < effect.GrantedTagCount; ++i)
            {
                const auto found = std::ranges::find(granted, effect.GrantedTags[i], &std::pair<GameplayTagId, std::uint16_t>::first);
                if (found == granted.end())
                    granted.emplace_back(effect.GrantedTags[i], 1);
                else
                    ++found->second;
            }
        });
        const GameplayTagContainer& tags = *world.TryGet<GameplayTagContainer>(wearer);
        for (std::uint8_t i = 0; i < tags.Count; ++i)
        {
            const auto found = std::ranges::find(granted, tags.Tags[i], &std::pair<GameplayTagId, std::uint16_t>::first);
            EXPECT_EQ(tags.ContributedCount(tags.Tags[i]), found != granted.end() ? found->second : 0u);
        }
    }

    struct FreshGateWorld : EquipmentGateTest
    {
        void TestBody() override {}
        void Tick() { Step(); }
        using EquipmentTest::Body;
        using EquipmentTest::Declare;
        using EquipmentTest::Entities;
        using EquipmentTest::Runtime;
        using EquipmentTest::Schema;
    };

    // The body as its prefab authors it: what a save's patch is taken against.
    EntityBuildPackage BodyPrefab(EquipmentLayoutId layout)
    {
        EntityBuildPackage prefab;
        const PackageEntityId root = prefab.CreateEntity();
        EXPECT_TRUE(prefab.AddComponent<ItemContainer>(root, ItemContainer{}));
        EXPECT_TRUE(prefab.AddComponent<GameplayTagContainer>(root, GameplayTagContainer{}));
        EXPECT_TRUE(prefab.AddComponent<AbilityGrants>(root, AbilityGrants{}));
        EXPECT_TRUE(prefab.AddComponent<AttributeSet>(root, AttributeSet{}));
        EXPECT_TRUE(prefab.AddComponent<EquipmentState>(
            root, EquipmentState{ .Layout = layout, .Count = 0, .NextKey = 1, .Records = {} }));
        return prefab;
    }
}

// The save path of docs/gameplay/saves.md: the body as a patch against its
// prefab (sencha.bodies), what it holds as held records (sencha.held).
TEST_F(EquipmentGateTest, WhatABodyWearsSurvivesASaveIntoAFreshWorldAndIsAttributable)
{
    World& world = Entities();
    const ShineRings rings = DefineShineRings(world);
    Declare();
    (void)world.TryGet<AttributeSet>(Hero)->Add(rings.Power, 1.0f);
    ASSERT_EQ(Gear().Equip(Hero, Give(rings.Sun), "ring.left").Refusal, EquipmentRefusal::None);
    ASSERT_EQ(Gear().Equip(Hero, Give(rings.Moon), "ring.right").Refusal, EquipmentRefusal::None);
    Step();
    ASSERT_EQ(std::as_const(world).TryGet<GameplayTagContainer>(Hero)->StackCount(rings.Shining), 2u);

    ComponentSerializerRegistry serializers;
    ComponentRegistrar registrar(nullptr, &serializers, nullptr);
    RegisterEngineComponents(registrar);
    LoggingProvider logging;
    SceneSerializationContext context{ logging };
    const SceneSpawnOverlay body = CaptureSpawnOverlay(world, Hero, BodyPrefab(Body), serializers, context);
    const JsonValue held = CaptureHeldRecords(world, std::span(&Hero, 1), serializers, context);
    const PersistentEntityId bodyId = world.TryGet<PersistentIdComponent>(Hero)->Id;

    FreshGateWorld fresh;
    World& restored = fresh.Entities();
    const ShineRings again = DefineShineRings(restored);
    fresh.Declare();
    EntityBuildPackage package = BodyPrefab(fresh.Body);
    std::string error;
    ASSERT_TRUE(ApplySpawnOverlay(package, body, serializers, error)) << error;
    RestoreHeldRecords(restored, held, serializers, context);
    ZoneImportError refused;
    ASSERT_TRUE(ImportZonePackage(fresh.Runtime, fresh.Schema, ZoneId{ 7 }, package, serializers, context,
                                  ZoneParticipation{ .Visible = false, .Physics = false, .Logic = true, .Audio = false },
                                  &refused))
        << refused.Message;
    EntityId wearer;
    restored.ForEachComponent<EquipmentState>([&](EntityId entity, const EquipmentState& state) {
        if (state.Count == 2)
            wearer = entity;
    });
    ASSERT_TRUE(wearer.IsValid());
    // As a spawn gives its root the id its overlay kept.
    restored.AddComponent<PersistentIdComponent>(wearer, PersistentIdComponent{ bodyId });
    fresh.Runtime.FlushLifecycleRequests();
    (void)fresh.Runtime.BeginResidencyProcessing();
    fresh.Runtime.FinalizeResidencyProcessing();
    ASSERT_EQ(std::as_const(restored).TryGet<EquipmentState>(wearer)->Count, 2u) << "the records came back";
    fresh.Tick();
    for (int i = 0; i < 3; ++i)
        (void)ReconcileEquipment(restored, wearer, FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = 100 });
    fresh.Tick();

    const auto stacks = [&](GameplayTagId tag) { return std::as_const(restored).TryGet<GameplayTagContainer>(wearer)->StackCount(tag); };
    const auto power = [&] {
        ResolveAttributesWithEffects(restored);
        return std::as_const(restored).TryGet<AttributeSet>(wearer)->GetCurrent(again.Power);
    };
    EXPECT_EQ(stacks(again.Shining), 2u) << "nothing contributed twice";
    EXPECT_EQ(stacks(again.Warded), 1u);
    EXPECT_FLOAT_EQ(power(), 6.0f);
    const AbilityGrants& grants = *std::as_const(restored).TryGet<AbilityGrants>(wearer);
    EXPECT_EQ(std::count_if(grants.Entries, grants.Entries + grants.Count,
                            [&](const AbilityGrant& grant) { return grant.Kind == AbilityGrantKind::Effect; }),
              1)
        << "the sun's ward, once";
    ExpectContributionsAttributable(restored, wearer);

    ASSERT_EQ(Equipment(restored, FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = 101 }).Unequip(wearer, "ring.right").Refusal,
              EquipmentRefusal::None);
    fresh.Tick();
    EXPECT_EQ(stacks(again.Shining), 1u) << "the moon's stack went, the sun's stayed";
    EXPECT_EQ(stacks(again.Warded), 1u);
    EXPECT_FLOAT_EQ(power(), 3.0f);
    ExpectContributionsAttributable(restored, wearer);
}

// S5.1's audit rows: an ability's owned tags are contributions, so a save holds none.
TEST_F(EquipmentGateTest, AnAbilitysOwnedTagIsAContributionASaveLeavesOut)
{
    ASSERT_EQ(Gear().Equip(Hero, Give(WardRing)).Refusal, EquipmentRefusal::None);
    Step();
    Request(Ward);
    Step();
    const GameplayTagContainer& tags = *std::as_const(Entities()).TryGet<GameplayTagContainer>(Hero);
    ASSERT_TRUE(tags.HasExact(Attacking)) << "the ward owns it while it runs";
    EXPECT_EQ(tags.BaselineCount(Attacking), 0u);
    JsonWriteArchive written;
    ASSERT_TRUE(WriteGameplayTags(written, tags, Entities().GetResource<GameplayTagRegistry>()));
    EXPECT_EQ(written.TakeValue().Find("tags")->AsArray().size(), 0u);
}
