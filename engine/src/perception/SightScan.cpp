#include "SightScan.h"

#include <perception/PerceptionFrame.h>
#include <physics/PhysicsQueries.h>
#include <spatial/sight/SightPlacement.h>

#include <algorithm>

SightScan::SightScan(const PhysicsWorld& physics)
    : Physics(physics)
{
}

void SightScan::Gather(const World& world, const StoragePartitionSet& partitions)
{
    if (QueriedWorld != &world)
    {
        Sensors.emplace(world);
        Targets.emplace(world);
        QueriedWorld = &world;
    }

    Placed.clear();
    Sensors->ForEachChunkIn(partitions, [this](auto& view) {
        const auto transforms = view.template Read<WorldTransform>();
        const auto sensors = view.template Read<SightSensor>();
        for (std::uint32_t row = 0; row < view.Count(); ++row)
            Placed.push_back({ view.Entity(row), PlaceSightObserver(sensors[row], transforms[row].Value) });
    });
    std::ranges::sort(Placed, {}, &PlacedObserver::Entity);

    PlacedTargets.clear();
    Targets->ForEachChunkIn(partitions, [this](auto& view) {
        const auto transforms = view.template Read<WorldTransform>();
        const auto targets = view.template Read<SightTarget>();
        for (std::uint32_t row = 0; row < view.Count(); ++row)
            PlacedTargets.push_back({ view.Entity(row), PlaceSightTarget(targets[row], transforms[row].Value) });
    });
    std::ranges::sort(PlacedTargets, {}, &PlacedTarget::Entity);

    ObserverIds.clear();
    for (const PlacedObserver& observer : Placed)
        ObserverIds.push_back(observer.Entity);
}

void SightScan::Run(const World& world, const StoragePartitionSet& partitions, const SightScanSettings& settings,
                    PerceptionFrame& frame, SightStats& stats, std::vector<SightTraceRow>& trace)
{
    if (!world.IsRegistered<SightSensor>() || !world.IsRegistered<SightTarget>())
    {
        Cursors.clear();
        return;
    }
    Gather(world, partitions);
    stats.Sensors = static_cast<std::uint32_t>(Placed.size());
    stats.Targets = static_cast<std::uint32_t>(PlacedTargets.size());

    const PhysicsQueries physics(Physics);
    for (const PlacedObserver& observer : Placed)
    {
        if (!observer.Eye)
        {
            ++stats.UnusableSensors;
            continue;
        }
        const SightObserver& eye = *observer.Eye;
        const bool traced = settings.Trace
            && (!settings.TracedObserver.IsValid() || settings.TracedObserver == observer.Entity);
        const EntityId ignore[] = { observer.Entity };
        const PhysicsQueryFilter filter{ .IgnoreEntities = ignore };

        Geometry.clear();
        InView.clear();
        for (std::uint32_t index = 0; index < PlacedTargets.size(); ++index)
        {
            const PlacedTarget& target = PlacedTargets[index];
            Geometry.push_back(target.Entity == observer.Entity ? SightOutcome::OutOfRange
                                                                : TestSightGeometry(eye, target.Sample));
            if (target.Entity != observer.Entity && Geometry.back() == SightOutcome::Seen)
                InView.push_back(index);
        }
        GrantRays(observer.Entity, settings.RaysPerObserver);

        frame.BeginScan(observer.Entity, settings.Sense);
        ScanCoverage coverage = ScanCoverage::Complete;
        for (std::uint32_t index = 0; index < PlacedTargets.size(); ++index)
        {
            const PlacedTarget& target = PlacedTargets[index];
            if (target.Entity == observer.Entity)
                continue;
            ++stats.Candidates;

            SightResult result{ .Outcome = Geometry[index] };
            if (result.Outcome == SightOutcome::Seen)
            {
                if (Granted[index] != 0)
                {
                    result = TestSight(physics, eye, target.Sample, target.Entity, filter);
                    stats.Raycasts += result.CastRay ? 1u : 0u;
                }
                else
                {
                    result.Outcome = SightOutcome::Untested;
                    coverage = ScanCoverage::Partial;
                }
            }

            switch (result.Outcome)
            {
            case SightOutcome::Seen:
                ++stats.Seen;
                frame.Add(target.Entity, target.Sample, 1.0f);
                break;
            case SightOutcome::OutOfRange: ++stats.OutOfRange; break;
            case SightOutcome::OutsideCone: ++stats.OutsideCone; break;
            case SightOutcome::Blocked: ++stats.Blocked; break;
            case SightOutcome::Untested: ++stats.Untested; break;
            }
            if (traced)
                trace.push_back({ .Observer = observer.Entity,
                                  .Target = target.Entity,
                                  .Eye = eye.Eye,
                                  .Sample = target.Sample,
                                  .Outcome = result.Outcome,
                                  .HitPoint = result.HitPoint });
        }
        frame.EndScan(coverage);
        stats.PartialScans += coverage == ScanCoverage::Partial ? 1u : 0u;
    }
    std::swap(Cursors, NextCursors);
    NextCursors.clear();
}

void SightScan::GrantRays(EntityId observer, std::uint32_t raysPerObserver)
{
    Granted.assign(PlacedTargets.size(), 0);
    if (InView.size() <= raysPerObserver)
    {
        for (const std::uint32_t index : InView)
            Granted[index] = 1;
        return;
    }

    // Rays go round the in-view targets in id order, starting after the last
    // one this observer tested, so each is tested within ceil(in view / rays)
    // ticks while the set holds still.
    const auto cursor = std::ranges::lower_bound(Cursors, observer, {}, &RayCursor::Observer);
    std::size_t first = 0;
    if (cursor != Cursors.end() && cursor->Observer == observer)
    {
        const auto after = std::ranges::upper_bound(InView, cursor->LastTested, {},
                                                    [this](std::uint32_t index) { return PlacedTargets[index].Entity; });
        first = after == InView.end() ? 0 : static_cast<std::size_t>(after - InView.begin());
    }
    std::uint32_t last = 0;
    for (std::uint32_t granted = 0; granted < raysPerObserver; ++granted)
    {
        last = InView[(first + granted) % InView.size()];
        Granted[last] = 1;
    }
    NextCursors.push_back({ observer, PlacedTargets[last].Entity });
}
