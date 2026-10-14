#include "authoring/AbilityPreviewLanes.h"

#include "authoring/AbilityElements.h"
#include "authoring/AbilityPreviewSession.h"

#include <abilities/AbilityDefinition.h>
#include <attributes/AttributeRegistry.h>
#include <ecs/World.h>

#include <algorithm>
#include <format>
#include <map>

namespace
{
double Seconds(std::uint64_t tick)
{
    return static_cast<double>(tick) / AbilityPreviewSession::kTickRate;
}

std::string LabelOf(const JsonValue* data, const std::string& key)
{
    const std::optional<AbilityElement> element = data != nullptr ? FindAbilityElement(*data, key) : std::nullopt;
    return element ? AbilityElementLabel(*data, *element) : key;
}

std::string_view ControlName(AbilityPreviewControl control)
{
    switch (control)
    {
    case AbilityPreviewControl::Press: return "press";
    case AbilityPreviewControl::Release: return "release";
    case AbilityPreviewControl::Interrupt: return "interrupt";
    case AbilityPreviewControl::Tap: return "tap";
    default: return {};
    }
}
} // namespace

WorkspaceTimelineFacet AbilityPreviewTimeline(const AbilityPreviewSession& preview, const JsonValue* data,
                                              std::string_view plotted)
{
    WorkspaceTimelineFacet facet;
    if (!preview.IsOpen())
        return facet;
    const std::deque<AbilityPreviewTickRecord>& history = preview.History();
    facet.Clock = "Preview";
    facet.Start = history.empty() ? 0.0 : Seconds(history.front().Tick);
    facet.End = std::max(facet.Start + Seconds(1), history.empty() ? 0.0 : Seconds(history.back().Tick + 1));
    facet.Time = Seconds(preview.Tick());
    facet.Playing = preview.IsPlaying();
    facet.Live = history.empty() || preview.Tick() == history.back().Tick;
    facet.Readout = std::format("tick {}", preview.Tick());

    const AbilityDefinition* definition = preview.Definition();
    const auto phaseName = [&](std::uint8_t phase) {
        return definition != nullptr && phase < definition->Phases.size() ? LabelOf(data, definition->Phases[phase].Key)
                                                                           : std::format("phase {}", phase);
    };
    TimelineLane phases{ .Label = "Phases" };
    // Each activation's open stay: the phase and the tick it began.
    std::map<std::uint64_t, std::pair<std::uint8_t, std::uint64_t>> open;
    const auto close = [&](std::uint64_t sequence, std::uint64_t end) {
        const auto [phase, start] = open.at(sequence);
        phases.Spans.push_back({ .Key = std::format("phase:{}:{}", sequence, start), .Start = Seconds(start),
                                 .End = Seconds(end), .Label = phaseName(phase) });
        open.erase(sequence);
    };
    for (const AbilityPreviewTickRecord& tick : history)
    {
        for (const AbilityRecord& record : tick.Records)
        {
            const bool live = record.State == AbilityRecordState::Live;
            const auto stay = open.find(record.Sequence);
            if (stay != open.end() && (!live || stay->second.first != record.Phase))
                close(record.Sequence, tick.Tick);
            if (live && !open.contains(record.Sequence))
                open[record.Sequence] = { record.Phase, tick.Tick };
        }
        for (auto it = open.begin(); it != open.end();)
        {
            const std::uint64_t sequence = it->first;
            ++it;
            if (std::ranges::none_of(tick.Records, [&](const AbilityRecord& r) { return r.Sequence == sequence; }))
                close(sequence, tick.Tick);
        }
        for (std::size_t i = 0; i < tick.Applied.size(); ++i)
            if (const std::string_view name = ControlName(tick.Applied[i].Control); !name.empty())
                facet.Marks.push_back({ .Key = std::format("input:{}:{}", tick.Tick, i), .Time = Seconds(tick.Tick),
                                        .Label = std::format("{} {}", name, tick.Applied[i].Name) });
        for (std::size_t i = 0; i < tick.Trace.size(); ++i)
        {
            const AbilityTraceRecord& traced = tick.Trace[i];
            if (traced.Event != AbilityTraceEvent::Operated || traced.Admission == VerbAdmission::Accepted)
                continue;
            const std::string op = definition != nullptr && traced.Element < definition->Ops.size()
                                     ? LabelOf(data, definition->Ops[traced.Element].Key)
                                     : std::string("op");
            facet.Marks.push_back({ .Key = std::format("op:{}:{}", tick.Tick, i), .Time = Seconds(tick.Tick),
                                    .Label = std::format("{}: {}", op, VerbAdmissionName(traced.Admission)),
                                    .Shape = TimelineMarkShape::Diamond });
        }
    }
    const std::uint64_t end = history.empty() ? 0 : history.back().Tick + 1;
    while (!open.empty())
        close(open.begin()->first, end);
    facet.Lanes.push_back(std::move(phases));

    const World* world = preview.SimulationWorld();
    const AttributeRegistry* attributes = world != nullptr ? world->TryGetResource<AttributeRegistry>() : nullptr;
    const AttributeId attribute =
        attributes != nullptr && !plotted.empty() ? attributes->FindAttribute(plotted) : AttributeId{};
    if (attribute.IsValid())
    {
        TimelineLane plot{ .Label = std::string(plotted) };
        for (const AbilityPreviewTickRecord& tick : history)
            if (attribute.Value - 1 < tick.Attributes.size())
                plot.Samples.push_back({ .Time = Seconds(tick.Tick), .Value = tick.Attributes[attribute.Value - 1] });
        facet.Lanes.push_back(std::move(plot));
    }
    return facet;
}
