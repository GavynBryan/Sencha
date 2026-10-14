#include <time/ActorTimeSystem.h>

#include <core/console/ConsoleRegistry.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>

#include <algorithm>
#include <cmath>

namespace
{
    using ScaledQuery = Query<Write<ActorTimeLeases>, Write<ActorTime>>;
    using ClockQuery = Query<Write<ActorTime>>;

    void Rescale(const World& world, ScaledQuery& scaled, const StoragePartitionSet* partitions, std::uint64_t tick,
                 const ActorScaleLimits& limits)
    {
        scaled.ForEachChunk([&](auto& view) {
            if (partitions != nullptr && !partitions->Contains(view.Partition()))
                return;
            auto leases = view.template Write<ActorTimeLeases>();
            auto times = view.template Write<ActorTime>();
            for (std::uint32_t i = 0; i < view.Count(); ++i)
            {
                ActorTimeLeases& held = leases[i];
                for (std::uint8_t l = 0; l < held.ActiveCount;)
                {
                    if (world.IsAlive(held.Active[l].Owner.Entity))
                        ++l;
                    else
                        ReleaseActorTimeLeases(held, held.Active[l].Owner, tick > 0 ? tick - 1 : 0);
                }
                PruneActorTimeHistory(held, tick);
                times[i].ScaleQ16 = ActorScaleAt(held, tick, limits);
            }
        });
    }

    void Accumulate(ClockQuery& clocks, const StoragePartitionSet* partitions)
    {
        clocks.ForEachChunk([&](auto& view) {
            if (partitions != nullptr && !partitions->Contains(view.Partition()))
                return;
            auto times = view.template Write<ActorTime>();
            for (std::uint32_t i = 0; i < view.Count(); ++i)
                times[i].LocalQ16 += times[i].ScaleQ16;
        });
    }

    using UndilatedQuery = Query<Read<ActorTime>, Without<ActorTimeDilated>>;
    using DilatedQuery = Query<Read<ActorTime>, With<ActorTimeDilated>>;

    // The marker follows the scale; it moves only when the scale crosses one, so a
    // steady dilation costs a scan and no structural change.
    void MarkDilation(World& world, UndilatedQuery& undilated, DilatedQuery& dilated,
                      const StoragePartitionSet* partitions, std::vector<EntityId>& changed)
    {
        const auto collect = [&](auto& query, bool wantDilated) {
            changed.clear();
            query.ForEachChunk([&](auto& view) {
                if (partitions != nullptr && !partitions->Contains(view.Partition()))
                    return;
                const auto times = view.template Read<ActorTime>();
                const EntityIndex* indices = view.Entities();
                for (std::uint32_t i = 0; i < view.Count(); ++i)
                    if ((times[i].ScaleQ16 != kActorTimeOne) == wantDilated)
                        changed.push_back(world.ResolveEntityIndex(indices[i]));
            });
        };
        collect(undilated, true);
        for (const EntityId entity : changed)
            world.AddComponent<ActorTimeDilated>(entity);
        collect(dilated, false);
        for (const EntityId entity : changed)
            world.RemoveComponent<ActorTimeDilated>(entity);
    }

    void RegisterLimit(ConsoleRegistry& console, const char* name, std::int64_t& value, const char* help)
    {
        const double scale = static_cast<double>(value) / static_cast<double>(kActorTimeOne);
        (void)console.RegisterCVar({
            .Name = name,
            .Owner = "engine",
            .Type = CVarType::Double,
            .DefaultValue = scale,
            .CurrentValue = scale,
            .Flags = CVarFlags::None,
            .Help = help,
            .Source = { "engine" },
            .Min = 0.01,
            .Max = static_cast<double>(kActorScaleHardMaximumQ16) / static_cast<double>(kActorTimeOne),
            .OnChange = [&value](const CVarChangeContext& ctx) {
                value = std::llround(std::get<double>(ctx.NewValue) * static_cast<double>(kActorTimeOne));
            },
        });
    }
}

ActorTimeSystem::ActorTimeSystem(ConsoleRegistry* console)
{
    if (console == nullptr)
        return;
    RegisterLimit(*console, "time.actor_scale_min", Bounds.MinimumQ16,
                  "The slowest any actor's time may run, as a fraction of the simulation's.");
    RegisterLimit(*console, "time.actor_scale_max", Bounds.MaximumQ16,
                  "The fastest any actor's time may run. A hastened character steps in as many substeps as this "
                  "rounds up to, so it is capped at four.");
}

void ActorTimeSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    if (!world.IsRegistered<ActorTime>())
        return;
    if (world.IsRegistered<ActorTimeLeases>())
    {
        if (!Scaled)
            Scaled.emplace(world);
        Rescale(world, *Scaled, &ctx.Partitions, ctx.Time.TickIndex, Bounds);
    }
    if (!Clocks)
        Clocks.emplace(world);
    Accumulate(*Clocks, &ctx.Partitions);
    if (world.IsRegistered<ActorTimeDilated>())
    {
        if (!Undilated)
        {
            Undilated.emplace(world);
            Dilated.emplace(world);
        }
        MarkDilation(world, *Undilated, *Dilated, &ctx.Partitions, Changed);
    }
    world.SetResource(Bounds);
}

void AdvanceActorTime(World& world, std::uint64_t tick, const ActorScaleLimits& limits)
{
    if (!world.IsRegistered<ActorTime>())
        return;
    if (world.IsRegistered<ActorTimeLeases>())
    {
        ScaledQuery scaled(world);
        Rescale(world, scaled, nullptr, tick, limits);
    }
    ClockQuery clocks(world);
    Accumulate(clocks, nullptr);
    if (world.IsRegistered<ActorTimeDilated>())
    {
        UndilatedQuery undilated(world);
        DilatedQuery dilated(world);
        std::vector<EntityId> changed;
        MarkDilation(world, undilated, dilated, nullptr, changed);
    }
    world.SetResource(limits);
}

std::uint64_t ActorTicksSince(const World& world, EntityId actor, std::uint64_t mark, std::uint64_t simulationTick)
{
    const ActorTime* time = world.IsRegistered<ActorTime>() ? world.TryGet<ActorTime>(actor) : nullptr;
    const std::uint64_t now = time != nullptr ? ActorTicks(*time) : simulationTick;
    return now > mark ? now - mark : 0;
}

std::int64_t ActorScaleOnTick(const World& world, EntityId actor, std::uint64_t tick)
{
    const ActorTimeLeases* leases =
        world.IsRegistered<ActorTimeLeases>() ? world.TryGet<ActorTimeLeases>(actor) : nullptr;
    if (leases == nullptr || !world.HasComponent<ActorTime>(actor))
        return kActorTimeOne;
    const ActorScaleLimits* limits = world.TryGetResource<ActorScaleLimits>();
    return ActorScaleAt(*leases, tick, limits != nullptr ? *limits : ActorScaleLimits{});
}
