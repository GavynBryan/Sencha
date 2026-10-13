#include "movement/MovementPreviewProperties.h"

#include "movement/MovementResolvePreview.h"
#include "movement/MovementResponseSim.h"

#include "properties/SettingProperties.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>

namespace
{
struct SupportChoice
{
    SupportKind Kind;
    std::string_view Label;
};
constexpr std::array<SupportChoice, 3> kSupportChoices{ {
    { SupportKind::Stable, "On stable ground" },
    { SupportKind::None, "In the air" },
    { SupportKind::Steep, "On steep ground" },
} };
constexpr std::string_view kTagPrefix = "tag:";
constexpr std::string_view kFreeMode = "movement.mode.free";

bool PatchTouches(const MovementTuningPatch& patch, const MovementTuningField& field)
{
    return (patch.*field.Patch).has_value();
}

// The rule that last moved this coefficient, which is the answer to "why is it
// that value?".
std::optional<std::size_t> FindWriter(const BoundMovementProfile& profile, std::span<const MovementLayerTrace> trace,
                                      const MovementTuningField& field)
{
    std::optional<std::size_t> writer;
    const std::size_t shared = std::min(profile.Layers.size(), trace.size());
    for (std::size_t index = 0; index < shared; ++index)
    {
        if (!trace[index].Matched)
            continue;
        const BoundMovementLayer& layer = profile.Layers[index];
        if (PatchTouches(layer.Set, field) || PatchTouches(layer.Scale, field) || PatchTouches(layer.Add, field))
            writer = index;
    }
    return writer;
}

// Trace entries are the shared layers in authored order, then the active
// mode's; an unnamed shared rule is described by its condition.
std::string TraceName(const MovementResolvePreview& preview, std::span<const MovementLayerTrace> trace,
                      std::size_t index)
{
    if (index >= trace.size())
        return {};
    if (!trace[index].Name.empty())
        return trace[index].Name;
    if (const CompiledMovementProfile* compiled = preview.Compiled(); compiled != nullptr && index < compiled->Layers.size())
        return SynthesizeMovementLayerName(compiled->Layers[index].When);
    return trace[index].SourcePath;
}

std::string Unbound(const MovementResolvePreview& preview)
{
    return preview.Error().empty() ? std::string("The profile does not compile") : preview.Error();
}
} // namespace

std::vector<PropertyNode> MovementResolveProperties::Nodes() const
{
    if (!Preview.IsBound())
        return { NoteProperty("unbound", "Resolve", Unbound(Preview)) };
    const MovementPreviewContext& context = Preview.Context();

    PropertyNode pretend = GroupProperty("pretend", "Pretend the character is");
    std::array<std::string_view, kSupportChoices.size()> supports{};
    std::size_t support = 0;
    for (std::size_t i = 0; i < kSupportChoices.size(); ++i)
    {
        supports[i] = kSupportChoices[i].Label;
        if (kSupportChoices[i].Kind == context.Support)
            support = i;
    }
    pretend.Children.push_back(ChoiceProperty("support", "Support", supports, support));
    pretend.Children.push_back(NumberProperty("immersion", "Immersion (%)", context.Immersion * 100.0, 0.0, 100.0, 1.0));
    pretend.Children.push_back(NumberProperty("move_speed", "Move speed (m/s)", context.MoveSpeed, 0.0, 100.0, 0.05,
                                              "Seeds Maximum speed before any rule runs; in game the MoveSpeed "
                                              "attribute supplies it, so effects can move it"));
    std::vector<std::string_view> modes(Preview.KnownModes().begin(), Preview.KnownModes().end());
    const std::string mode = context.Mode.empty() ? std::string(kFreeMode) : context.Mode;
    if (std::ranges::find(modes, mode) == modes.end())
        modes.insert(modes.begin(), mode);
    pretend.Children.push_back(ChoiceProperty(
        "mode", "Mode", modes, static_cast<std::size_t>(std::ranges::find(modes, mode) - modes.begin())));
    for (const std::string& tag : Preview.KnownTags())
        pretend.Children.push_back(ToggleProperty(std::string(kTagPrefix) + tag, tag,
                                                  std::ranges::find(context.ActiveTags, tag) != context.ActiveTags.end()));

    const MovementResolveResult resolved = Preview.Resolve();
    PropertyNode rules = GroupProperty("rules", "Rules");
    for (std::size_t index = 0; index < resolved.Trace.size(); ++index)
    {
        const MovementLayerTrace& entry = resolved.Trace[index];
        rules.Children.push_back(NoteProperty(
            std::format("rule:{}", index), TraceName(Preview, resolved.Trace, index),
            entry.Matched ? std::string("Active")
                          : entry.Failure.empty() ? std::string("Off") : std::format("Off: {}", entry.Failure)));
    }

    PropertyNode result = GroupProperty("result", "Resulting coefficients");
    const ResolvedMovementTuning defaults;
    for (const MovementTuningField& field : MovementTuningFields())
    {
        const auto label = std::ranges::find(Labels, field.Key, &MovementCoefficientLabel::Key);
        const std::string name = label == Labels.end() ? std::string(field.Key)
                               : label->Units.empty() ? label->Display
                                                      : std::format("{} ({})", label->Display, label->Units);
        std::string from;
        if (const std::optional<std::size_t> writer = FindWriter(*Preview.BoundProfile(), resolved.Trace, field))
            from = "set by " + TraceName(Preview, resolved.Trace, *writer);
        else if (field.Key == "max_speed")
            from = "the MoveSpeed attribute";
        else
            from = std::format("engine default {}", FormatMovementCoefficient(defaults.*field.Resolved));
        result.Children.push_back(NoteProperty(std::format("result:{}", field.Key), name,
                                               std::format("{}, {}", FormatMovementCoefficient(resolved.Tuning.*field.Resolved), from)));
    }
    return { std::move(pretend), std::move(rules), std::move(result) };
}

PropertyEditResult MovementResolveProperties::Apply(std::string_view key, std::string_view value)
{
    MovementPreviewContext& context = Preview.Context();
    if (key == "support")
    {
        const auto found = std::ranges::find(kSupportChoices, value, &SupportChoice::Label);
        if (found == kSupportChoices.end())
            return PropertyEditResult::Refused;
        context.Support = found->Kind;
    }
    else if (key == "immersion" || key == "move_speed")
    {
        const std::optional<double> number = ParseNumber(value, 0.0, 100.0);
        if (!number.has_value())
            return PropertyEditResult::Refused;
        (key == "immersion" ? context.Immersion : context.MoveSpeed) =
            static_cast<float>(key == "immersion" ? *number / 100.0 : *number);
    }
    else if (key == "mode")
        context.Mode = value == kFreeMode ? std::string{} : std::string(value);
    else if (key.starts_with(kTagPrefix))
    {
        const std::optional<bool> held = ParseToggle(value);
        if (!held.has_value())
            return PropertyEditResult::Refused;
        const std::string tag(key.substr(kTagPrefix.size()));
        std::erase(context.ActiveTags, tag);
        if (*held)
            context.ActiveTags.push_back(tag);
    }
    else
        return PropertyEditResult::Gone;
    return PropertyEditResult::Applied;
}

std::vector<PropertyNode> MovementResponseProperties::Nodes() const
{
    if (!Preview.IsBound())
        return { NoteProperty("unbound", "Response", Unbound(Preview)) };
    MovementSimParams params;
    params.MoveSpeed = Preview.Context().MoveSpeed;
    const MovementSimContextFn makeContext = [this](SupportKind support, std::span<const std::string> tags,
                                                    GameplayTagContainer& storage) {
        const std::vector<std::string> names(tags.begin(), tags.end());
        return Preview.MakeResolveContext(support, 0.0f, names, storage);
    };
    const MovementJumpArc arc = SimulateJumpArc(*Preview.BoundProfile(), makeContext, params);
    const MovementSpeedResponse response = SimulateSpeedResponse(*Preview.BoundProfile(), makeContext, params);
    const auto metric = [](std::string key, std::string label, float value, std::string_view unit, std::string tooltip) {
        PropertyNode node = NoteProperty(std::move(key), std::move(label),
                                         std::format("{} {}", FormatMovementCoefficient(value), unit));
        node.Descriptor.Tooltip = std::move(tooltip);
        return node;
    };
    std::vector<PropertyNode> nodes{
        metric("jump_height", "Jump height", arc.ApexHeight, "m", "Peak height above the launch point"),
        metric("time_to_apex", "Time to apex", arc.TimeToApex, "s", "How long the rise takes"),
        metric("hang_time", "Hang time", arc.HangTime, "s", "Launch until back at launch height"),
        metric("hop_distance", "Hop distance", arc.Distance, "m", "Ground covered over one hop, stick held forward"),
        metric("top_speed", "Top speed", response.TopSpeed, "m/s", "Where ground acceleration and friction settle"),
        metric("stopping_distance", "Stopping distance", response.StoppingDistance, "m",
               "How far the character slides once the stick is let go"),
        metric("jump_speed", "Jump speed", arc.JumpSpeed, "m/s", "The resolved launch speed"),
    };
    nodes.insert(nodes.begin() + 5,
                 response.ReachedTopSpeed
                     ? metric("time_to_top_speed", "Time to top speed", response.TimeToTopSpeed, "s",
                              "From standing to within 5% of top speed")
                     : NoteProperty("time_to_top_speed", "Time to top speed", "Not reached"));
    if (!arc.Landed)
        nodes.push_back(NoteProperty("landed", "Landing", "The character never came back down within the simulated window"));
    return nodes;
}
