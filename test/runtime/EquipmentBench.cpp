// Records what equipment costs per tick: the ability pass over equipped
// instances against the same effects applied directly, the movement tag
// projections, a waiting record's rebinding with unrelated holders changing,
// and the first pass after many wearers' records arrive at once. Env-gated:
// SENCHA_EQUIPMENT_BENCH_OUT names the JSON written (a .csv beside it); run it
// from the profile preset (docs/plans/evidence/equipment-overlay/results.md).

#include "BenchRecorder.h"

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySystem.h>
#include <core/json/JsonParser.h>
#include <ecs/WorldComponentSchema.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <equipment/Equipment.h>
#include <equipment/EquipmentKit.h>
#include <equipment/EquipmentSystem.h>
#include <equipment/WornGrantEntries.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <inventory/InventoryKit.h>
#include <inventory/InventorySystem.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementModeSystems.h>
#include <movement/MovementTags.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/CharacterMovement.h>
#include <world/RuntimeComponentSchema.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistableRef.h>
#include <world/registry/Registry.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

namespace
{
    Bench::Recorder Recorder;
    constexpr double kTickSeconds = 1.0 / 60.0;
    int Reps(int fallback) { return Bench::RepsFromEnvironment("SENCHA_EQUIPMENT_BENCH_REPS", fallback); }

    constexpr std::string_view kInstant = R"({
        "phases": [ { "key": "p_go", "then": [ { "key": "t_end", "to": "end" } ] } ] })";

    // An authority World with a layout of two ring slots and a holding use.x,
    // rings whose effect grants one ability, and bombs that hold their slot.
    struct EquipmentWorld
    {
        explicit EquipmentWorld(std::size_t ringSlots = 2, std::size_t grantsPerRing = 1)
        {
            WorldComponentSchema schema;
            RegisterEngineRuntimeComponents(schema);
            schema.Seal();
            schema.Apply(Entities());
            Entities().AddResource<PersistentEntityIndex>();
            Entities().AdvanceFrame();
            RegisterAbilityKit(Entities());
            InstallInventoryVocabulary(Entities());
            InstallEquipmentVocabulary(Entities());
            GameplayTagRegistry& tags = Entities().GetResource<GameplayTagRegistry>();
            Ring = *tags.RegisterTag("Item.Ring");
            Usable = *tags.RegisterTag("Item.Usable");
            EquipmentLayout layout;
            std::vector<std::string> keys;
            for (std::size_t ring = 0; ring < ringSlots; ++ring)
                keys.push_back(std::format("ring.{}", ring));
            keys.push_back("use.x");
            for (const std::string& key : keys)
            {
                EquipmentSlot slot;
                slot.Key = key;
                slot.Accepts = std::string_view(key).starts_with("ring") ? Ring : Usable;
                slot.OnDepleted = std::string_view(key) == "use.x" ? SlotDepletion::Hold : SlotDepletion::Release;
                layout.Slots.push_back(std::move(slot));
            }
            Body = Entities().GetResource<EquipmentLayoutRegistry>().Define("asset://equipment/body.sdata", std::move(layout));
            AbilityDeclaration declaration;
            std::vector<AssetRef> dependencies;
            std::vector<AbilityDiagnostic> diagnostics;
            EXPECT_TRUE(CompileAbilityDeclaration(*JsonParse(kInstant), declaration, dependencies, diagnostics));
            AbilityDefinition definition;
            EXPECT_TRUE(ResolveAbilityDeclaration(declaration, "asset://abilities/bolt.sdata",
                                                  MakeAbilityResolveEnvironment(Entities()), definition, diagnostics));
            (void)Entities().GetResource<AbilityRegistry>().Define("asset://abilities/bolt.sdata", std::move(definition));
            EffectDefinition lasting{};
            lasting.Duration = EffectDuration::Infinite;
            Glow = Entities().GetResource<EffectRegistry>().Define("asset://effects/glow.sdata", lasting);
            std::vector<AbilityGrantDeclaration> granted;
            for (std::size_t g = 0; g < grantsPerRing; ++g)
                granted.push_back({ .Key = std::format("g_bolt{}", g), .Ability = "asset://abilities/bolt.sdata", .Override = {} });
            (void)DeclareEffectGrants(Entities(), Glow, granted);
            ItemRegistry& items = Entities().GetResource<ItemRegistry>();
            RingItem = items.Define("asset://items/ring.sdata",
                                    ItemDefinition{ .Name = "Ring", .Tags = { Ring }, .Occupies = { "ring" },
                                                    .EquipEffects = { { Glow, 1.0f } } });
            Bomb = items.Define("asset://items/bomb.sdata", ItemDefinition{ .Name = "Bomb", .Tags = { Usable }, .StackMax = 5,
                                                                            .Occupies = { "use.x" } });
            Pebble = items.Define("asset://items/pebble.sdata", ItemDefinition{ .Name = "Pebble" });
            std::vector<std::string> errors;
            DeclareWornGrantEntries(Entities(), errors);
            EXPECT_TRUE(errors.empty());
        }

        World& Entities() { return Scene.Components; }

        EntityId Wearer()
        {
            const EntityId wearer = Entities().CreateEntity();
            Entities().AddComponent<ItemContainer>(wearer, ItemContainer{});
            Entities().AddComponent<GameplayTagContainer>(wearer, GameplayTagContainer{});
            Entities().AddComponent<AbilityGrants>(wearer, AbilityGrants{});
            Entities().AddComponent<EquipmentState>(wearer, EquipmentState{ .Layout = Body, .Count = 0, .NextKey = 1, .Records = {} });
            return wearer;
        }

        FixedSimTime Time() const { return FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = Now }; }

        // Microseconds `fn` takes, averaged over `ticks` calls.
        template <typename Fn>
        double Average(int ticks, Fn&& fn)
        {
            double total = 0.0;
            for (int i = 0; i < ticks; ++i)
            {
                const auto start = std::chrono::steady_clock::now();
                fn();
                total += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
                ++Now;
            }
            return total / ticks;
        }

        Registry Scene;
        GameplayTagId Ring, Usable;
        EquipmentLayoutId Body;
        EffectId Glow;
        ItemId RingItem, Bomb, Pebble;
        std::uint64_t Now = 0;
    };

    // The ability pass and the equipment pass over `wearers`, each wearing one
    // ring through Equipment, or holding it with the ring's effect applied
    // directly: the per-instance grant table's own cost is the difference.
    void RecordOverlay(std::size_t wearers, bool viaEquipment)
    {
        EquipmentWorld bench;
        World& world = bench.Entities();
        for (std::size_t i = 0; i < wearers; ++i)
        {
            const EntityId wearer = bench.Wearer();
            const EntityId ring = Inventory(world).Give(wearer, bench.RingItem, 1).Entry;
            if (viaEquipment)
                EXPECT_EQ(Equipment(world, bench.Time()).Equip(wearer, ring).Refusal, EquipmentRefusal::None);
            else
                EXPECT_TRUE(ApplyEffect(world, wearer, bench.Glow, bench.Time(), LeaseOwner{ .Entity = ring, .Key = 1 }).IsValid());
        }
        const auto tick = [&] {
            RunAbilityPass(world, AbilityHost{}, bench.Time());
            RunEquipmentPass(world, bench.Time());
        };
        (void)bench.Average(5, tick);
        const int ticks = Reps(100);
        const std::string mode = viaEquipment ? "equipped" : "direct";
        Recorder.Record(std::format("overlay.{}.{}.ability_pass", mode, wearers), "us",
                        bench.Average(ticks, [&] { RunAbilityPass(world, AbilityHost{}, bench.Time()); }));
        Recorder.Record(std::format("overlay.{}.{}.equipment_pass", mode, wearers), "us",
                        bench.Average(ticks, [&] { RunEquipmentPass(world, bench.Time()); }));
    }

    // `wearers` each wearing `records` rings, every ring granting `grantsPerRing`
    // abilities: the two passes once everything is derived.
    void RecordWorn(std::size_t wearers, std::size_t records, std::size_t grantsPerRing)
    {
        EquipmentWorld bench(records, grantsPerRing);
        World& world = bench.Entities();
        for (std::size_t i = 0; i < wearers; ++i)
        {
            const EntityId wearer = bench.Wearer();
            for (std::size_t r = 0; r < records; ++r)
            {
                const EntityId ring = Inventory(world).Give(wearer, bench.RingItem, 1).Entry;
                EXPECT_EQ(Equipment(world, bench.Time()).Equip(wearer, ring).Refusal, EquipmentRefusal::None);
            }
        }
        const auto tick = [&] {
            RunAbilityPass(world, AbilityHost{}, bench.Time());
            RunEquipmentPass(world, bench.Time());
        };
        (void)bench.Average(5, tick);
        const int ticks = Reps(100);
        const std::string scenario = std::format("worn.wearers_{}.records_{}.grants_{}", wearers, records, records * grantsPerRing);
        Recorder.Record(scenario + ".ability_pass", "us",
                        bench.Average(ticks, [&] { RunAbilityPass(world, AbilityHost{}, bench.Time()); }));
        Recorder.Record(scenario + ".equipment_pass", "us",
                        bench.Average(ticks, [&] { RunEquipmentPass(world, bench.Time()); }));
    }

    // The mode and support projections over `characters` that stand still: the
    // per-tick read that finds nothing to write.
    void RecordProjection(std::size_t characters)
    {
        World world;
        world.RegisterComponent<GameplayTagContainer>();
        world.RegisterComponent<CharacterMovement>();
        world.RegisterComponent<SupportState>();
        GameplayTagRegistry& tags = world.AddResource<GameplayTagRegistry>();
        LocomotionModeRegistry& modes = world.AddResource<LocomotionModeRegistry>(tags);
        const LocomotionModeId free = modes.RegisterFree();
        for (const char* name : { "movement.mode.flight", "movement.mode.swim", "movement.mode.climb" })
            (void)modes.Register<SupportState>(name);
        world.AddResource<MovementTags>(RegisterMovementTags(tags));
        for (std::size_t i = 0; i < characters; ++i)
        {
            const EntityId character = world.CreateEntity();
            world.AddComponent<GameplayTagContainer>(character, GameplayTagContainer{});
            world.AddComponent<CharacterMovement>(character, CharacterMovement{ .Mode = free });
            SupportState stable{};
            stable.Kind = SupportKind::Stable;
            *world.TryGet<SupportState>(character) = stable;
        }
        const auto step = [&] {
            ModeTagProjectionSystem{}.Step(world);
            SupportTagProjectionSystem{}.Step(world);
        };
        step();
        double total = 0.0;
        const int ticks = Reps(100);
        for (int i = 0; i < ticks; ++i)
        {
            const auto start = std::chrono::steady_clock::now();
            step();
            total += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        }
        Recorder.Record(std::format("projection.{}.still", characters), "us", total / ticks);
    }

    // `holders` wearers each with a record waiting in use.x, and one of them
    // given a pebble every tick: the walks the pass takes, and its time.
    void RecordUnrelatedHolders(std::size_t holders)
    {
        EquipmentWorld bench;
        World& world = bench.Entities();
        std::vector<EntityId> wearers;
        for (std::size_t i = 0; i < holders; ++i)
        {
            const EntityId wearer = bench.Wearer();
            wearers.push_back(wearer);
            for (int pebble = 0; pebble < 20; ++pebble)
                (void)Inventory(world).Give(wearer, bench.Pebble, 1);
            const EntityId bomb = Inventory(world).Give(wearer, bench.Bomb, 1).Entry;
            EXPECT_EQ(Equipment(world, bench.Time()).Equip(wearer, bomb).Refusal, EquipmentRefusal::None);
            world.TryGet<ItemEntry>(bomb)->Count = 0;
            NoteDepleted(world, bomb, EntityId{});
        }
        RunEquipmentPass(world, bench.Time());
        RunInventoryPass(world);
        RunEquipmentPass(world, bench.Time());
        const std::uint64_t before = EquipmentRebindWalks(world);
        const int ticks = Reps(100);
        const double pass = bench.Average(ticks, [&] {
            (void)Inventory(world).Give(wearers.front(), bench.Pebble, 1);
            RunEquipmentPass(world, bench.Time());
        });
        Recorder.Record(std::format("rebind.{}.walks_per_tick", holders), "walks",
                        static_cast<double>(EquipmentRebindWalks(world) - before) / ticks);
        Recorder.Record(std::format("rebind.{}.equipment_pass", holders), "us", pass);
    }

    // `wearers` whose two records each arrive together, as a zone's NPCs do when
    // it loads: the first pass derives every one of them.
    void RecordAdoptionBurst(std::size_t wearers)
    {
        EquipmentWorld bench;
        World& world = bench.Entities();
        for (std::size_t i = 0; i < wearers; ++i)
        {
            const EntityId wearer = bench.Wearer();
            // Giving moves the wearer's row, so its records are written once, after.
            EquipmentRecordList worn = CopyEquipment(world, wearer);
            for (std::uint8_t slot = 0; slot < 2; ++slot)
            {
                const EntityId ring = Inventory(world).Give(wearer, bench.RingItem, 1).Entry;
                worn.Records.push_back({ .Key = worn.NextKey++, .Definition = bench.RingItem, .Slots = 1u << slot,
                                         .Primary = slot, .Source = MakePersistableRef(world, ring),
                                         .Derivation = EquipmentDerivation::Pending });
            }
            SetEquipment(world, wearer, worn);
        }
        const auto start = std::chrono::steady_clock::now();
        RunEquipmentPass(world, bench.Time());
        Recorder.Record(std::format("adoption.{}.first_pass", wearers), "us",
                        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
        Recorder.Record(std::format("adoption.{}.steady_pass", wearers), "us",
                        bench.Average(Reps(100), [&] { RunEquipmentPass(world, bench.Time()); }));
    }
}

TEST(EquipmentBench, Generate)
{
    const char* out = std::getenv("SENCHA_EQUIPMENT_BENCH_OUT");
    if (out == nullptr)
        GTEST_SKIP() << "set SENCHA_EQUIPMENT_BENCH_OUT to record the equipment bench";
    Recorder.Clear();
    for (const std::size_t wearers : { 250u, 1000u, 2000u })
    {
        RecordOverlay(wearers, false);
        RecordOverlay(wearers, true);
    }
    for (const std::size_t characters : { 1000u, 4000u })
        RecordProjection(characters);
    for (const std::size_t holders : { 100u, 1000u })
        RecordUnrelatedHolders(holders);
    RecordAdoptionBurst(200);
    RecordWorn(1000, 4, 1);
    RecordWorn(100, 16, 1);
    const std::filesystem::path json(out);
    EXPECT_TRUE(Recorder.WriteJson(json));
    EXPECT_TRUE(Recorder.WriteCsv(std::filesystem::path(json).replace_extension(".csv")));
}
