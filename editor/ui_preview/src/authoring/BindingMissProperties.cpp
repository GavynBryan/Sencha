#include "authoring/BindingMissProperties.h"

#include "authoring/BindingMisses.h"
#include "authoring/UiPreviewSession.h"
#include "properties/PropertyTree.h"
#include "properties/SettingProperties.h"

#include <format>

namespace
{
std::vector<BindingMiss> Misses(const UiPreviewSession& session)
{
    const std::deque<UiDiagnostic>& history = session.Diagnostics();
    const std::vector<UiDiagnostic> diagnostics(history.begin(), history.end());
    return CollectBindingMisses(diagnostics, session.Model());
}

std::string_view KindLabel(BindingMissKind kind)
{
    switch (kind)
    {
    case BindingMissKind::Variable: return "Variable";
    case BindingMissKind::Member:   return "Member";
    case BindingMissKind::Action:   return "Action";
    }
    return "Variable";
}

// The row list a member was read through, when the model declares no list of
// that name: declaring it makes the member a row's.
bool DeclarableParent(const BindingMiss& miss, const UiPreviewModel& model)
{
    return !miss.Parent.empty() && !model.DeclaresRows(miss.Parent) && !model.DeclaresArray(miss.Parent);
}
} // namespace

std::vector<PropertyNode> BindingMissProperties::Nodes() const
{
    std::vector<PropertyNode> nodes;
    const std::vector<BindingMiss> misses = Misses(Session);
    for (std::size_t i = 0; i < misses.size(); ++i)
    {
        const BindingMiss& miss = misses[i];
        const std::string name = miss.Parent.empty() ? miss.Name : miss.Parent + "." + miss.Name;
        PropertyNode node = NoteProperty(std::format("miss.{}", i), std::string(KindLabel(miss.Kind)), name);
        switch (miss.Kind)
        {
        case BindingMissKind::Variable:
            node.Descriptor.Verbs = { { "property", "Declare as Property" },
                                      { "list", "Declare as List" },
                                      { "rows", "Declare as Row List" } };
            break;
        case BindingMissKind::Member:
            if (DeclarableParent(miss, Session.Model()))
                node.Descriptor.Verbs = { { "rows", std::format("Declare {} as Row List", miss.Parent) } };
            else
                node.Descriptor.Tooltip = "Not a row member: label, value, detail, editable, control, number, min, "
                                          "max, step, choices";
            break;
        case BindingMissKind::Action:
            node.Descriptor.Verbs = { { "action", "Declare as Action" } };
            break;
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

PropertyEditResult BindingMissProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    const std::vector<BindingMiss> misses = Misses(Session);
    const std::optional<PropertyKeyIndex> picked = ParsePropertyKeyIndex(key, "miss.");
    if (!picked || !picked->Rest.empty() || picked->Index >= misses.size())
        return PropertyEditResult::Gone;
    if (!Model.IsOpen())
        return PropertyEditResult::Refused;
    const BindingMiss& miss = misses[picked->Index];
    UiPreviewModel& model = Session.Model();
    if (verb == "property" && miss.Kind == BindingMissKind::Variable)
        model.Properties.push_back(UiModelProperty{ .Path = miss.Name, .Initial = UiValue(miss.Name) });
    else if (verb == "list" && miss.Kind == BindingMissKind::Variable)
        model.Arrays.push_back({ miss.Name, { "first", "second" } });
    else if (verb == "rows" && miss.Kind == BindingMissKind::Variable)
        model.Rows.push_back({ miss.Name, { UiPreviewModel::SampleRow() } });
    else if (verb == "rows" && miss.Kind == BindingMissKind::Member && DeclarableParent(miss, model))
        model.Rows.push_back({ miss.Parent, { UiPreviewModel::SampleRow() } });
    else if (verb == "action" && miss.Kind == BindingMissKind::Action)
        model.Actions.push_back(miss.Name);
    else
        return PropertyEditResult::Refused;
    // The remade screen reports again what is still missing.
    (void)Session.Reopen();
    Model.Changed();
    return PropertyEditResult::Applied;
}
