// Env-gated; run by scripts/bench_replication.sh from the profile preset.

#include "BenchRecorder.h"
#include "AllocationCounter.h"

#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityRegistry.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <ecs/ArchetypeSignature.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSnapshotAck.h>
#include <net/ReplicationCodec.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationRuntime.h>
#include <net/ReplicationSnapshot.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <malloc.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // The shape an inventory entry would have; nothing here is an inventory.
    struct BenchEntry
    {
        EntityId Container;
        std::uint32_t Definition = 0;
        std::uint32_t Count = 0;
        std::uint16_t Position = 0;
    };

    struct BenchEntryWire
    {
        std::uint64_t Container = 0;
        std::uint32_t Definition = 0;
        std::uint32_t Count = 0;
        std::uint16_t Position = 0;
    };
}

template <>
struct TypeSchema<BenchEntry>
{
    static constexpr std::string_view Name = "bench.entry";
    static auto Fields() { return std::tuple{ MakeField("count", &BenchEntry::Count) }; }
};

template <>
struct TypeSchema<BenchEntryWire>
{
    static constexpr std::string_view Name = "bench.entry_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("container", &BenchEntryWire::Container).OwnerOnly(),
                           MakeField("definition", &BenchEntryWire::Definition).OwnerOnly(),
                           MakeField("count", &BenchEntryWire::Count).OwnerOnly(),
                           MakeField("position", &BenchEntryWire::Position).OwnerOnly() };
    }
};

template <>
struct ReplicationCodec<BenchEntry>
{
    using Wire = BenchEntryWire;
    static void ToWire(const ReplicationWireContext& context, const BenchEntry& local, BenchEntryWire& wire)
    {
        wire.Container = context.WireEntity(local.Container);
        wire.Definition = local.Definition;
        wire.Count = local.Count;
        wire.Position = local.Position;
    }
    static void FromWire(const ReplicationWireContext& context, const BenchEntryWire& wire, BenchEntry& local)
    {
        local.Container = context.LocalEntity(wire.Container);
        local.Definition = wire.Definition;
        local.Count = wire.Count;
        local.Position = wire.Position;
    }
};

namespace
{
    Bench::Recorder Recorder;

    int Reps(int fallback) { return Bench::RepsFromEnvironment("SENCHA_REPLICATION_BENCH_REPS", fallback); }

    // Live heap bytes (glibc): every container below allocates on the heap.
    std::int64_t HeapBytes()
    {
        return static_cast<std::int64_t>(mallinfo2().uordblks);
    }

    enum class Carried : std::uint8_t
    {
        Entry,              // the component alone
        Persistent,         // and a persistent id, indexed
        Replicated,         // and NetReplicated + NetOwner
    };

    // Built at the final signature, so no row migrates on the way.
    struct Authority
    {
        Authority()
        {
            ComponentRegistrar components(&Schema, nullptr, &Layout);
            RegisterEngineComponents(components);
            components.Add<BenchEntry>();
            Schema.Seal();
            Layout.Seal();
            Schema.Apply(Entities);
            Entities.AddResource<PersistentEntityIndex>();
        }

        void Make(std::size_t count, std::size_t containers, Carried carried)
        {
            for (std::size_t c = 0; c < containers; ++c)
                Containers.push_back(Entities.CreateEntity());
            ArchetypeSignature signature;
            signature.set(Entities.GetComponentId<BenchEntry>());
            if (carried != Carried::Entry)
                signature.set(Entities.GetComponentId<PersistentIdComponent>());
            if (carried == Carried::Replicated)
            {
                signature.set(Entities.GetComponentId<NetReplicated>());
                signature.set(Entities.GetComponentId<NetOwner>());
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::size_t container = i % containers;
                const EntityId entry = Entities.CreateEntityWithSignature(signature);
                (void)Entities.InitializeComponent<BenchEntry>(
                    entry, BenchEntry{ .Container = Containers[container],
                                       .Definition = static_cast<std::uint32_t>(i % 97),
                                       .Count = 1,
                                       .Position = static_cast<std::uint16_t>(i / containers) });
                if (carried != Carried::Entry)
                    (void)Entities.InitializeComponent<PersistentIdComponent>(
                        entry, PersistentIdComponent{ PersistentEntityId{ i + 1 } });
                if (carried == Carried::Replicated)
                    (void)Entities.InitializeComponent<NetOwner>(
                        entry, NetOwner{ .Peer = static_cast<std::uint32_t>(container + 1) });
                Entries.push_back(entry);
            }
        }

        void MakeActors(std::size_t count, std::size_t grants)
        {
            RegisterAbilityKit(Entities);
            std::vector<std::pair<AbilityId, std::uint16_t>> abilities;
            for (std::size_t g = 0; g < grants; ++g)
            {
                const std::string path = std::format("asset://bench/ability_{}.sdata", g);
                const AbilityId ability = Entities.GetResource<AbilityRegistry>().Define(path, AbilityDefinition{});
                abilities.emplace_back(
                    ability, Entities.GetResource<AbilityGrantEntries>().Intern({ std::format("g_{}", g), ability, {} }));
            }
            AttributeRegistry& names = Entities.GetResource<AttributeRegistry>();
            AttributeSet attributes;
            for (const char* name : { "Health", "Energy", "Armor", "MoveSpeed" })
                (void)attributes.Add(names.RegisterAttribute(name), 50.0f);
            for (std::size_t i = 0; i < count; ++i)
            {
                const EntityId actor = Entities.CreateEntity();
                Entities.AddComponent<NetReplicated>(actor);
                Entities.AddComponent<LocalTransform>(actor, LocalTransform{});
                if (grants != 0)
                {
                    Entities.AddComponent<AbilityGrants>(actor, AbilityGrants{});
                    Entities.AddComponent<AttributeSet>(actor, attributes);
                    for (const auto& [ability, entry] : abilities)
                        (void)GrantAbility(Entities, actor, ability, AbilityGrantKind::Authored, entry);
                }
                Entries.push_back(actor);
            }
        }

        WorldComponentSchema Schema;
        ReplicationLayout Layout;
        World Entities;
        std::vector<EntityId> Containers;
        std::vector<EntityId> Entries;
    };

    // The client only acknowledges, so what is timed is the authority's half.
    struct Publisher
    {
        Publisher(Authority& authority, std::size_t peers)
            : World_(authority)
            , Peers(peers)
            , Acks(peers)
            , Scratch(kNetMaxSnapshotBytes)
        {
        }

        struct Cost
        {
            double UpdateMs = 0.0;
            double WriteMs = 0.0;
            std::size_t Allocations = 0;
            std::size_t Bytes = 0;
            std::size_t Written = 0;
            std::size_t Deferred = 0;
        };

        Cost Publish()
        {
            Cost cost;
            const std::size_t allocationsBefore = AllocationCount();
            auto start = Bench::Clock::now();
            Changes.Update(World_.Entities, World_.Layout, Identity, ++Generation);
            cost.UpdateMs = Bench::MillisecondsSince(start);
            start = Bench::Clock::now();
            ++Tick;
            for (std::size_t p = 0; p < Peers.size(); ++p)
            {
                SnapshotWriteRequest write;
                write.Changes = &Changes;
                write.Layout = &World_.Layout;
                write.Peer = &Peers[p];
                write.OwnerPeer = static_cast<std::uint32_t>(p + 1);
                write.Tick = Tick;
                write.Sequence = Peers[p].NextSnapshotSequence();
                const SnapshotWriteResult written = ReplicationWriteSnapshot(write, Scratch);
                cost.Bytes += written.BytesWritten;
                cost.Written += written.EntitiesWritten;
                cost.Deferred += written.EntitiesDeferred + written.EntitiesUnsendable;
                Acks[p].Observe(write.Sequence);
                Peers[p].Acknowledge(Acks[p]);
            }
            cost.WriteMs = Bench::MillisecondsSince(start);
            cost.Allocations = AllocationCount() - allocationsBefore;
            return cost;
        }

        std::size_t Seed()
        {
            const std::size_t entities = World_.Entries.size();
            std::size_t publishes = 0;
            while (publishes < 100000)
            {
                (void)Publish();
                ++publishes;
                bool seeded = true;
                for (const ReplicationPeerState& peer : Peers)
                    seeded = seeded && peer.Size() >= entities;
                if (seeded)
                    break;
            }
            return publishes;
        }

        Authority& World_;
        ReplicationAuthorityIdentity Identity;
        ReplicationChangeStore Changes;
        std::uint64_t Generation = 0;
        std::uint64_t Tick = 0;
        std::vector<ReplicationPeerState> Peers;
        std::vector<NetSnapshotAck> Acks;
        std::vector<std::byte> Scratch;
    };

    void MeasureMemory(std::size_t count)
    {
        const auto perEntry = [count](std::int64_t bytes) {
            return static_cast<double>(bytes) / static_cast<double>(count);
        };
        std::int64_t entry = 0;
        std::int64_t persistent = 0;
        std::int64_t replicated = 0;
        for (const Carried carried : { Carried::Entry, Carried::Persistent, Carried::Replicated })
        {
            auto authority = std::make_unique<Authority>();
            const std::int64_t empty = HeapBytes();
            authority->Make(count, 16, carried);
            const std::int64_t made = HeapBytes() - empty;
            if (carried == Carried::Entry)
                entry = made;
            else if (carried == Carried::Persistent)
                persistent = made;
            else
                replicated = made;

            if (carried != Carried::Replicated)
                continue;
            Publisher publisher(*authority, 16);
            const std::int64_t beforeUpdate = HeapBytes();
            (void)publisher.Publish();
            const std::int64_t afterFirst = HeapBytes();
            (void)publisher.Seed();
            const std::int64_t seeded = HeapBytes();
            Recorder.Record(std::format("entries_{}.bytes_per_entry.change_store_and_identity", count), "bytes",
                            perEntry(afterFirst - beforeUpdate));
            Recorder.Record(std::format("entries_{}.bytes_per_entry.peer_floors_16", count), "bytes",
                            perEntry(seeded - afterFirst));
        }
        Recorder.Record(std::format("entries_{}.bytes_per_entry.entity", count), "bytes", perEntry(entry));
        Recorder.Record(std::format("entries_{}.bytes_per_entry.persistent_identity", count), "bytes",
                        perEntry(persistent - entry));
        Recorder.Record(std::format("entries_{}.bytes_per_entry.replication_components", count), "bytes",
                        perEntry(replicated - persistent));
    }

    void MeasureStructural(std::size_t batch, int reps)
    {
        std::vector<double> create;
        std::vector<double> move;
        std::vector<double> destroy;
        for (int rep = 0; rep < reps; ++rep)
        {
            Authority authority;
            auto start = Bench::Clock::now();
            authority.Make(batch, 16, Carried::Replicated);
            create.push_back(Bench::MillisecondsSince(start));
            start = Bench::Clock::now();
            for (const EntityId entry : authority.Entries)
                (void)authority.Entities.MoveEntityToPartition(entry, StoragePartitionId{ 1 });
            move.push_back(Bench::MillisecondsSince(start));
            (void)authority.Entities.ConsumePartitionMoves();
            start = Bench::Clock::now();
            for (const EntityId entry : authority.Entries)
                authority.Entities.DestroyEntity(entry);
            destroy.push_back(Bench::MillisecondsSince(start));
        }
        Recorder.Record(std::format("structural.batch_{}.create", batch), "ms", Bench::Median(create));
        Recorder.Record(std::format("structural.batch_{}.move_partition", batch), "ms", Bench::Median(move));
        Recorder.Record(std::format("structural.batch_{}.destroy", batch), "ms", Bench::Median(destroy));
    }

    void MeasurePublish(std::size_t count, std::size_t peers, double changingFraction, int reps)
    {
        Authority authority;
        authority.Make(count, peers, Carried::Replicated);
        Publisher publisher(authority, peers);
        (void)publisher.Seed();

        const std::size_t changing = static_cast<std::size_t>(static_cast<double>(count) * changingFraction);
        std::vector<double> update;
        std::vector<double> write;
        std::vector<double> allocations;
        std::vector<double> bytes;
        for (int rep = 0; rep < 30 + reps; ++rep)
        {
            for (std::size_t i = 0; i < changing; ++i)
                ++authority.Entities.TryGet<BenchEntry>(authority.Entries[(rep * changing + i) % count])->Count;
            const Publisher::Cost cost = publisher.Publish();
            if (rep < 30)
                continue;
            update.push_back(cost.UpdateMs);
            write.push_back(cost.WriteMs);
            allocations.push_back(static_cast<double>(cost.Allocations));
            bytes.push_back(static_cast<double>(cost.Bytes));
        }
        const std::string scenario = std::format("publish.entries_{}.peers_{}.changing_{}pct", count, peers,
                                                 static_cast<int>(changingFraction * 100.0));
        Recorder.Record(scenario + ".change_store_update", "ms", Bench::Median(update));
        Recorder.Record(scenario + ".write_all_peers", "ms", Bench::Median(write));
        Recorder.Record(scenario + ".allocations", "count", Bench::Median(allocations));
        Recorder.Record(scenario + ".bytes_all_peers", "bytes", Bench::Median(bytes));
    }

    double MeasureImageWork(Publisher& publisher, int reps)
    {
        World& world = publisher.World_.Entities;
        const ReplicationLayout& layout = publisher.World_.Layout;
        std::vector<ComponentId> columns(layout.Size());
        for (std::size_t i = 0; i < layout.Size(); ++i)
        {
            const ReplicatedComponent* component = layout.At(static_cast<std::uint8_t>(i));
            if (world.IsRegistered(component->Type))
                columns[i] = world.GetComponentIdByType(component->Type);
        }
        std::vector<std::byte> scratch;
        std::vector<std::byte> copy;
        std::size_t equal = 0;
        std::vector<double> work;
        for (int rep = 0; rep < 30 + reps; ++rep)
        {
            const auto start = Bench::Clock::now();
            for (const ReplicationChangeStore::EntityState& entity : publisher.Changes.Live())
            {
                const EntityId local = publisher.Identity.TryResolve(entity.Id);
                for (const ReplicationChangeStore::ComponentState& held : entity.Components)
                {
                    const ReplicatedComponent& component = *layout.At(held.WireIndex);
                    const auto* raw = static_cast<const std::byte*>(world.GetComponentRaw(local, columns[held.WireIndex]));
                    if (component.ToWire != nullptr)
                    {
                        scratch.assign(component.Size, std::byte{ 0 });
                        const ReplicationWireContext context{ .Entities = &world, .Entity = local,
                                                              .Authority = &publisher.Identity };
                        component.ToWire(context, std::span(raw, component.LocalSize), scratch);
                    }
                    else
                    {
                        scratch.assign(raw, raw + component.Size);
                    }
                    ReplicationSnapToWire(component, scratch);
                    equal += std::memcmp(held.Bytes.data(), scratch.data(), component.Size) == 0 ? 1 : 0;
                    copy.assign(held.Bytes.begin(), held.Bytes.end());
                }
            }
            if (rep >= 30)
                work.push_back(Bench::MillisecondsSince(start));
        }
        EXPECT_GT(equal, 0u);
        return Bench::Median(work);
    }

    void MeasureActors(std::size_t count, std::size_t grants, int reps)
    {
        Authority authority;
        authority.MakeActors(count, grants);
        Publisher publisher(authority, 1);
        (void)publisher.Seed();

        std::size_t touched = 0;
        for (const ReplicationChangeStore::EntityState& entity : publisher.Changes.Live())
            for (const ReplicationChangeStore::ComponentState& held : entity.Components)
                touched += held.Bytes.size();

        const std::string scenario = std::format("actors_{}.grants_{}", count, grants);
        for (const bool moving : { false, true })
        {
            std::vector<double> update;
            std::size_t bytes = 0;
            std::size_t written = 0;
            std::size_t deferred = 0;
            for (int rep = 0; rep < 30 + reps; ++rep)
            {
                if (moving)
                    for (const EntityId actor : authority.Entries)
                        authority.Entities.TryGet<LocalTransform>(actor)->Value.Position.X += 0.25f;
                const Publisher::Cost cost = publisher.Publish();
                if (rep < 30)
                    continue;
                update.push_back(cost.UpdateMs);
                bytes += cost.Bytes;
                written += cost.Written;
                deferred += cost.Deferred;
            }
            const std::string state = scenario + (moving ? ".moving" : ".still");
            Recorder.Record(state + ".change_store_update", "ms", Bench::Median(update));
            if (!moving)
                continue;
            Recorder.Record(state + ".bytes_per_written_actor", "bytes",
                            static_cast<double>(bytes) / static_cast<double>(std::max<std::size_t>(written, 1)));
            Recorder.Record(state + ".actors_written_per_publish", "count",
                            static_cast<double>(written) / static_cast<double>(reps));
            Recorder.Record(state + ".actors_deferred_per_publish", "count",
                            static_cast<double>(deferred) / static_cast<double>(reps));
        }
        Recorder.Record(scenario + ".image_bytes_touched_per_publish", "bytes", static_cast<double>(touched));
        Recorder.Record(scenario + ".image_build_compare_copy", "ms", MeasureImageWork(publisher, reps));
        Recorder.Record(scenario + ".image_bytes_per_actor", "bytes",
                        static_cast<double>(touched) / static_cast<double>(count));
    }

    void MeasureSeeding(std::size_t count, std::size_t peers)
    {
        Authority authority;
        authority.Make(count, peers, Carried::Replicated);
        Publisher publisher(authority, peers);
        const std::size_t publishes = publisher.Seed();
        const double seconds = static_cast<double>(publishes * kNetDefaultSnapshotInterval) / 60.0;
        const std::string scenario = std::format("seed.entries_{}.peers_{}", count, peers);
        Recorder.Record(scenario + ".publishes", "count", static_cast<double>(publishes));
        Recorder.Record(scenario + ".seconds_at_60hz", "s", seconds);
    }
}

TEST(EntityReplicationBench, Generate)
{
    const char* out = std::getenv("SENCHA_REPLICATION_BENCH_OUT");
    if (out == nullptr)
        GTEST_SKIP() << "set SENCHA_REPLICATION_BENCH_OUT to record the entity replication bench "
                        "(use scripts/bench_replication.sh)";
    const std::filesystem::path json(out);
    if (json.has_parent_path())
        std::filesystem::create_directories(json.parent_path());

    Recorder.Clear();
    Recorder.Record("entry.sizeof", "bytes", static_cast<double>(sizeof(BenchEntry)));
    Recorder.Record("entry.sizeof_wire", "bytes", static_cast<double>(sizeof(BenchEntryWire)));
    for (const std::size_t count : { std::size_t{ 1000 }, std::size_t{ 10000 }, std::size_t{ 50000 } })
        MeasureMemory(count);
    for (const std::size_t batch : { std::size_t{ 1 }, std::size_t{ 100 }, std::size_t{ 10000 } })
        MeasureStructural(batch, Reps(30));
    for (const std::size_t count : { std::size_t{ 0 }, std::size_t{ 1000 }, std::size_t{ 5000 }, std::size_t{ 10000 } })
        for (const std::size_t peers : { std::size_t{ 1 }, std::size_t{ 16 } })
            for (const double changing : { 0.0, 0.01 })
                MeasurePublish(count, peers, changing, Reps(120));
    MeasureSeeding(300, 1);
    MeasureSeeding(150 * 16, 16);
    for (const std::size_t count : { std::size_t{ 300 }, std::size_t{ 1000 } })
        for (const std::size_t grants : { std::size_t{ 0 }, std::size_t{ 3 } })
            MeasureActors(count, grants, Reps(120));

    ASSERT_TRUE(Recorder.WriteJson(json)) << "cannot write " << json.generic_string();
    std::filesystem::path csv = json;
    csv.replace_extension(".csv");
    ASSERT_TRUE(Recorder.WriteCsv(csv)) << "cannot write " << csv.generic_string();
}
