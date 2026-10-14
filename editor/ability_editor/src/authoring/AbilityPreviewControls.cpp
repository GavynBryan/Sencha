#include "authoring/AbilityPreviewControls.h"

#include "authoring/AbilityPreviewSession.h"
#include "properties/SettingProperties.h"

#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <ecs/World.h>
#include <time/ActorTime.h>

#include <format>

namespace
{
constexpr std::string_view kInput = "input.";
constexpr std::string_view kAttribute = "attribute.";
constexpr std::string_view kGrantTag = "tag.grant";
constexpr std::string_view kRevokeTag = "tag.revoke";
constexpr std::string_view kTimeScale = "time.scale";
constexpr std::string_view kTarget = "target";

PropertyNode Verbs(PropertyNode node, std::initializer_list<std::pair<std::string_view, std::string_view>> verbs)
{
    for (const auto& [id, label] : verbs)
        node.Descriptor.Verbs.push_back({ .Id = std::string(id), .Label = std::string(label) });
    return node;
}
} // namespace

std::vector<PropertyNode> AbilityPreviewControls::Nodes() const
{
    const World* world = Preview.SimulationWorld();
    if (world == nullptr || !Preview.Fixture().IsValid())
        return {};
    std::vector<PropertyNode> nodes;
    for (const std::string& action : Preview.InputActions())
        nodes.push_back(Verbs(NoteProperty(std::format("{}{}", kInput, action), action, "on the next tick"),
                              { { "press", "Press" }, { "release", "Release" }, { "tap", "Tap" },
                                { "tap3", "Tap \xC3\x97" "3" }, { "interrupt", "Interrupt" } }));
    const AttributeRegistry* attributes = world->TryGetResource<AttributeRegistry>();
    const AttributeSet* set = world->TryGet<AttributeSet>(Preview.Fixture());
    PropertyNode group = GroupProperty("attributes", "Attributes");
    for (std::uint32_t id = 1; attributes != nullptr && id <= attributes->Size() && set != nullptr; ++id)
    {
        const AttributeId attribute{ id };
        const std::string name(attributes->GetName(attribute));
        PropertyNode value = NumberProperty(std::format("{}{}", kAttribute, name), name, set->GetBase(attribute),
                                            attributes->Min(attribute), attributes->Max(attribute), 1.0);
        if (name != PlottedAttribute)
            value.Descriptor.Verbs.push_back({ .Id = "plot", .Label = "Plot" });
        group.Children.push_back(std::move(value));
    }
    if (!group.Children.empty())
        nodes.push_back(std::move(group));
    nodes.push_back(TextProperty(std::string(kGrantTag), "Grant tag", {}, "A tag the fixture is given on the next tick"));
    nodes.push_back(TextProperty(std::string(kRevokeTag), "Revoke tag", {}, "A tag taken from the fixture"));
    const ActorTime* time = world->TryGet<ActorTime>(Preview.Fixture());
    nodes.push_back(NumberProperty(std::string(kTimeScale), "Time scale",
                                   time != nullptr ? ActorScaleValue(time->ScaleQ16) : 1.0, 0.1, 4.0, 0.1));
    nodes.push_back(Verbs(NoteProperty(std::string(kTarget), "Target", "a body with every attribute, along the aim"),
                          { { "place3", "Place 3 m ahead" }, { "place10", "Place 10 m ahead" } }));
    return nodes;
}

PropertyEditResult AbilityPreviewControls::CommitEdit(std::string_view key, std::string_view value)
{
    if (key.starts_with(kAttribute))
    {
        const std::optional<double> number = ParseNumber(value);
        if (!number)
            return PropertyEditResult::Refused;
        Preview.Control({ .Control = AbilityPreviewControl::SetAttribute,
                          .Name = std::string(key.substr(kAttribute.size())), .Value = *number });
        return PropertyEditResult::Applied;
    }
    if (key == kGrantTag || key == kRevokeTag)
    {
        if (value.empty())
            return PropertyEditResult::Unchanged;
        Preview.Control({ .Control = key == kGrantTag ? AbilityPreviewControl::GrantTag : AbilityPreviewControl::RevokeTag,
                          .Name = std::string(value) });
        return PropertyEditResult::Applied;
    }
    if (key == kTimeScale)
    {
        const std::optional<double> scale = ParseNumber(value, 0.1, 4.0);
        if (!scale)
            return PropertyEditResult::Refused;
        Preview.Control({ .Control = AbilityPreviewControl::TimeScale, .Value = *scale });
        return PropertyEditResult::Applied;
    }
    return PropertyEditResult::Gone;
}

PropertyEditResult AbilityPreviewControls::Act(std::string_view key, std::string_view verb, std::string_view)
{
    if (key.starts_with(kInput))
    {
        const std::string action(key.substr(kInput.size()));
        AbilityPreviewAction control{ .Name = action };
        if (verb == "press")
            control.Control = AbilityPreviewControl::Press;
        else if (verb == "release")
            control.Control = AbilityPreviewControl::Release;
        else if (verb == "interrupt")
            control.Control = AbilityPreviewControl::Interrupt;
        else if (verb == "tap" || verb == "tap3")
        {
            control.Control = AbilityPreviewControl::Tap;
            control.Count = verb == "tap3" ? 3 : 1;
        }
        else
            return PropertyEditResult::Gone;
        Preview.Control(std::move(control));
        return PropertyEditResult::Applied;
    }
    if (key.starts_with(kAttribute) && verb == "plot")
    {
        PlottedAttribute = std::string(key.substr(kAttribute.size()));
        return PropertyEditResult::Applied;
    }
    if (key == kTarget && (verb == "place3" || verb == "place10"))
    {
        Preview.Control({ .Control = AbilityPreviewControl::PlaceTarget, .Value = verb == "place3" ? 3.0 : 10.0 });
        return PropertyEditResult::Applied;
    }
    return PropertyEditResult::Gone;
}
