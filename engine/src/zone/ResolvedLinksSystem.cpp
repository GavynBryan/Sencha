#include <zone/ResolvedLinksSystem.h>

#include <app/GameContexts.h>
#include <core/console/CVarRead.h>
#include <core/console/ConsoleRegistry.h>
#include <core/logging/Logger.h>
#include <debug/DebugLines.h>
#include <ecs/EntityText.h>
#include <world/RuntimeWorld.h>
#include <zone/ResolvedLinks.h>

#include <format>
#include <span>
#include <string>

namespace
{
    constexpr std::string_view kDebugDrawCVar = "world.links.debug.draw";
    constexpr std::string_view kDebugChannel = "world.links";
    const Vec4 kUprightColor{ 0.3f, 0.85f, 0.95f, 1.0f };
    const Vec4 kTiltedColor{ 0.95f, 0.55f, 0.2f, 1.0f };
    const Vec4 kMappingColor{ 0.95f, 0.9f, 0.3f, 1.0f };
    constexpr float kNormalLength = 0.5f;
    constexpr float kMarkerDepth = 0.5f;
    constexpr float kMarkerSize = 0.15f;

    void DrawRectangle(DebugLineList& lines, const ApertureFrame& frame, const Vec4& color)
    {
        const Vec3d right = frame.Right * frame.HalfExtents.X;
        const Vec3d up = frame.Up * frame.HalfExtents.Y;
        const Vec3d corners[] = { frame.Origin - right - up, frame.Origin + right - up, frame.Origin + right + up,
                                  frame.Origin - right + up };
        for (int corner = 0; corner < 4; ++corner)
            lines.AddOnTop(corners[corner], corners[(corner + 1) % 4], color);
        lines.AddOnTop(frame.Origin, frame.Origin + frame.Normal * kNormalLength, color);
    }

    ConsoleResult DescribeLinks(const World& world)
    {
        ConsoleResult result;
        const ResolvedLinks* links = world.TryGetResource<ResolvedLinks>();
        if (links == nullptr || links->States().empty())
        {
            result.Info("no world links");
            return result;
        }
        const ResolvedLinksStats& stats = links->Stats();
        result.Info(std::format("{} active, {} awaiting, {} invalid, {} demand only; {} rebuilds", stats.Active,
                                stats.Awaiting, stats.Invalid, stats.DemandOnly, stats.TotalRebuilds));
        for (const LinkState& state : links->States())
        {
            std::string line = std::format("{} {} -> {} {}", LinkIdToString(state.Id), ZoneIdToString(state.ZoneA),
                                           ZoneIdToString(state.ZoneB), LinkResolutionName(state.Resolution));
            if (state.Resolution == LinkResolution::Invalid)
                line += std::format(" ({})", LinkFaultName(state.Fault));
            if (const ResolvedLink* resolved = links->Find(state.Id); resolved != nullptr && !resolved->Upright)
                line += " (not upright)";
            line += " entity ";
            AppendEntityText(line, state.Link);
            result.Info(std::move(line));
        }
        return result;
    }
}

ResolvedLinksSystem::ResolvedLinksSystem(RuntimeWorld& runtime, ConsoleRegistry* console, Logger* log)
    : Runtime(runtime)
    , Console(console)
    , Log(log)
{
    World& world = runtime.Entities();
    if (!world.HasResource<ResolvedLinks>())
        world.AddResource<ResolvedLinks>();
    if (console == nullptr)
        return;
    console->RegisterCVar({
        .Name = std::string(kDebugDrawCVar),
        .Owner = "world",
        .Type = CVarType::Bool,
        .DefaultValue = false,
        .CurrentValue = false,
        .Help = "Draws each active link's two apertures with their front normals, and a marker behind one "
                "aperture with where the link carries it.",
        .Source = { "world" },
    });
    (void)console->RegisterCommand({
        .Name = "links",
        .Owner = "world",
        .Usage = "links",
        .Help = "Prints every world link's state and the resolution counters.",
        .Callback = [&world](ConsoleExecutionContext&, std::span<const std::string>) { return DescribeLinks(world); },
    });
}

void ResolvedLinksSystem::PreSimulate(PreSimulateContext&)
{
    ResolvedLinks& links = Runtime.Entities().GetResource<ResolvedLinks>();
    links.Update(Runtime);
    if (Log != nullptr)
        for (const LinkTransition& transition : links.Transitions())
            if (transition.To == LinkResolution::Invalid)
                Log->Warn("world link {} {}", LinkIdToString(transition.Id), LinkFaultName(transition.Fault));
    Draw();
}

void ResolvedLinksSystem::Draw()
{
    const bool draw = ReadCVarBool(Console, kDebugDrawCVar, false);
    if (!draw && !Drawing)
        return;
    World& world = Runtime.Entities();
    if (!world.HasResource<DebugLineChannels>())
        world.AddResource<DebugLineChannels>();
    DebugLineChannels& channels = world.GetResource<DebugLineChannels>();
    DebugLineList& lines = channels.Lines(channels.Channel(kDebugChannel));
    lines.Clear();
    Drawing = draw;
    if (!draw)
        return;
    for (const ResolvedLink& link : world.GetResource<ResolvedLinks>().Active())
    {
        const Vec4& color = link.Upright ? kUprightColor : kTiltedColor;
        DrawRectangle(lines, link.FrameA, color);
        DrawRectangle(lines, link.FrameB, color);
        const Vec3d behindA = link.FrameA.Origin - link.FrameA.Normal * kMarkerDepth;
        AppendMarker(lines, behindA, kMarkerSize, kMappingColor);
        AppendMarker(lines, link.AToB.TransformPoint(behindA), kMarkerSize, kMappingColor);
    }
}
