#include <perception/PerceptionSystem.h>

#include "SightScan.h"

#include <core/console/CVarRead.h>
#include <core/console/ConsoleRegistry.h>
#include <debug/DebugLines.h>
#include <perception/PerceptionFrame.h>
#include <perception/PerceptionVocabulary.h>
#include <world/SimulationAuthority.h>

#include <vector>

namespace
{
    constexpr std::string_view kRaysPerObserverCVar = "perception.sight.rays_per_observer";
    constexpr std::string_view kDebugDrawCVar = "perception.debug.draw";
    constexpr std::string_view kDebugChannel = "perception";
    constexpr std::int64_t kDefaultRaysPerObserver = 32;

    const Vec4 kConeColor{ 0.35f, 0.55f, 0.9f, 1.0f };
    const Vec4 kSeenColor{ 0.2f, 0.95f, 0.35f, 1.0f };
    const Vec4 kBlockedColor{ 0.95f, 0.25f, 0.2f, 1.0f };
    const Vec4 kOccludedColor{ 0.45f, 0.15f, 0.12f, 1.0f };
    const Vec4 kUntestedColor{ 0.95f, 0.8f, 0.2f, 1.0f };
    const Vec4 kRejectedColor{ 0.4f, 0.4f, 0.4f, 1.0f };
    constexpr float kFacingLength = 0.75f;
    constexpr float kMarkerSize = 0.3f;

    void RegisterCVars(ConsoleRegistry& console)
    {
        console.RegisterCVar({
            .Name = std::string(kRaysPerObserverCVar),
            .Owner = "perception",
            .Type = CVarType::Int,
            .DefaultValue = kDefaultRaysPerObserver,
            .CurrentValue = kDefaultRaysPerObserver,
            .Help = "Sight rays one observer may cast per fixed tick. An observer with more candidates "
                    "in range and view than this reports a partial scan.",
            .Source = { "perception" },
            .Min = 1.0,
            .Max = 4096.0,
        });
        console.RegisterCVar({
            .Name = std::string(kDebugDrawCVar),
            .Owner = "perception",
            .Type = CVarType::Bool,
            .DefaultValue = false,
            .CurrentValue = false,
            .Help = "Draws every sight sensor's cone, and the sight lines of the observer selected in the "
                    "Perception panel (every observer's when none is selected).",
            .Source = { "perception" },
        });
    }

    void DrawSight(DebugLineList& lines, std::span<const SightScan::PlacedObserver> observers,
                   std::span<const SightTraceRow> trace, EntityId selected, const PerceptionFrame& frame)
    {
        for (const SightScan::PlacedObserver& observer : observers)
        {
            if (!observer.Eye)
                continue;
            const SightObserver& eye = *observer.Eye;
            AppendCone(lines, eye.Eye, eye.Forward, eye.HalfAngle, eye.Range, kConeColor);
            lines.AddOnTop(eye.Eye, eye.Eye + eye.Forward * kFacingLength, kConeColor);
        }
        for (const SightTraceRow& row : trace)
        {
            switch (row.Outcome)
            {
            case SightOutcome::Seen: lines.AddTested(row.Eye, row.Sample, kSeenColor); break;
            case SightOutcome::Blocked:
                lines.AddTested(row.Eye, row.HitPoint, kBlockedColor);
                lines.AddOnTop(row.HitPoint, row.Sample, kOccludedColor);
                break;
            case SightOutcome::Untested: lines.AddOnTop(row.Eye, row.Sample, kUntestedColor); break;
            case SightOutcome::OutOfRange:
            case SightOutcome::OutsideCone:
                if (selected.IsValid())
                    lines.AddOnTop(row.Eye, row.Sample, kRejectedColor);
                break;
            }
        }
        for (const SenseScan& scan : frame.Scans())
        {
            if (selected.IsValid() && scan.Observer != selected)
                continue;
            for (const Observation& observation : frame.ObservationsOf(scan))
                AppendMarker(lines, observation.Position, kMarkerSize, kSeenColor);
        }
    }
}

struct PerceptionSystem::State
{
    State(const PhysicsWorld& physics, ConsoleRegistry* console)
        : Sight(physics), Console(console)
    {
    }

    SightScan Sight;
    ConsoleRegistry* Console = nullptr;
    SightStats Stats;
    std::vector<SightTraceRow> Trace;
    EntityId Selected;
    bool Drawing = false;
};

PerceptionSystem::PerceptionSystem(const PhysicsWorld& physics, ConsoleRegistry* console)
    : Impl(std::make_unique<State>(physics, console))
{
    if (console != nullptr)
        RegisterCVars(*console);
}

PerceptionSystem::~PerceptionSystem() = default;

void PerceptionSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    const PerceptionSenses& senses = EnsurePerceptionSenses(world);
    if (!world.HasResource<PerceptionFrame>())
        world.AddResource<PerceptionFrame>();
    PerceptionFrame& frame = world.GetResource<PerceptionFrame>();

    const bool draw = ReadCVarBool(Impl->Console, kDebugDrawCVar, false);
    Impl->Stats = {};
    Impl->Trace.clear();
    frame.Begin(ctx.Time.TickIndex);
    if (IsSimulationAuthority(world))
    {
        const SightScanSettings settings{
            .Sense = senses.Sight,
            .RaysPerObserver = static_cast<std::uint32_t>(
                ReadCVarInt(Impl->Console, kRaysPerObserverCVar, kDefaultRaysPerObserver)),
            .Trace = draw || Impl->Selected.IsValid(),
            .TracedObserver = Impl->Selected,
        };
        Impl->Sight.Run(world, ctx.Partitions, settings, frame, Impl->Stats, Impl->Trace);
    }
    frame.Publish();

    if (!draw && !Impl->Drawing)
        return;
    if (!world.HasResource<DebugLineChannels>())
        world.AddResource<DebugLineChannels>();
    DebugLineChannels& channels = world.GetResource<DebugLineChannels>();
    DebugLineList& lines = channels.Lines(channels.Channel(kDebugChannel));
    lines.Clear();
    if (draw)
        DrawSight(lines, Impl->Sight.PlacedObservers(), Impl->Trace, Impl->Selected, frame);
    Impl->Drawing = draw;
}

const SightStats& PerceptionSystem::LastSightStats() const
{
    return Impl->Stats;
}

void PerceptionSystem::SelectObserver(EntityId observer)
{
    Impl->Selected = observer;
}

EntityId PerceptionSystem::SelectedObserver() const
{
    return Impl->Selected;
}

std::span<const SightTraceRow> PerceptionSystem::LastTrace() const
{
    return Impl->Trace;
}

std::span<const EntityId> PerceptionSystem::LastObservers() const
{
    return Impl->Sight.Observers();
}
