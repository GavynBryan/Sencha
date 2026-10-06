#include "actions/ContextMenuModel.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "properties/PropertySource.h"

#include <algorithm>

namespace
{
constexpr std::size_t kChoiceLimit = 4;
constexpr std::size_t kActionLimit = 3;
constexpr std::size_t kSummaryLimit = 4;

bool Offered(const PropertyNode& node, bool assets)
{
    const PropertyDescriptor& d = node.Descriptor;
    if (d.ReadOnly || d.Advanced)
        return false;
    return (d.Kind == PropertyKind::Choice && d.Choices.size() > 1) || d.Kind == PropertyKind::Toggle
        || (d.Kind == PropertyKind::Asset && assets);
}

bool ReadsOut(const PropertyNode& node)
{
    const PropertyKind kind = node.Descriptor.Kind;
    return kind != PropertyKind::Group && kind != PropertyKind::Unsupported && !node.Descriptor.Advanced
        && !node.Value.empty();
}

template <typename Visit>
void Walk(const std::vector<PropertyNode>& nodes, Visit&& visit)
{
    for (const PropertyNode& node : nodes)
    {
        visit(node);
        Walk(node.Children, visit);
    }
}
} // namespace

ContextMenuModel::ContextMenuModel(Hooks hooks, const ActionCatalog& catalog)
    : Callbacks(std::move(hooks))
    , Catalog(catalog)
{
}

void ContextMenuModel::Invoke(const std::string& action) const
{
    if (const ActionContextStack* actions = Callbacks.Actions ? Callbacks.Actions() : nullptr)
        (void)actions->Invoke(action);
}

void ContextMenuModel::Opening(Vec2d pointer)
{
    if (Callbacks.Aim)
        Callbacks.Aim(pointer);
    Build(Callbacks.Selection ? Callbacks.Selection() : WorkspaceSelectionFacet{});
}

void ContextMenuModel::Build(const WorkspaceSelectionFacet& selection)
{
    Entries.clear();
    Fans.clear();
    Lines.clear();
    if (selection.Title.empty())
        return;
    Lines.push_back(selection.Title);

    std::size_t choices = 0;
    const bool assets = Callbacks.Recent && Callbacks.Browse;
    const auto gather = [&](const std::vector<PropertySection>& sections, bool details) {
        for (std::size_t section = 0; section < sections.size(); ++section)
        {
            if (sections[section].Source == nullptr)
                continue;
            Walk(sections[section].Source->Nodes(), [&](const PropertyNode& node) {
                if (Lines.size() <= kSummaryLimit && ReadsOut(node) && !details)
                    Lines.push_back(node.Descriptor.Label + "  " + node.Value);
                if (choices >= kChoiceLimit || !Offered(node, assets))
                    return;
                Entry entry{ .Kind = EntryKind::Choice, .Label = node.Descriptor.Label, .InDetails = details,
                             .Section = section, .Key = node.Key };
                if (node.Descriptor.Kind == PropertyKind::Asset)
                {
                    entry.Kind = EntryKind::Asset;
                    std::vector<AssetChoice> offered = Callbacks.Recent(node.Descriptor.Asset);
                    if (std::ranges::none_of(offered, [&](const AssetChoice& c) { return c.Path == node.Value; }))
                        offered.insert(offered.begin(),
                                       { .Path = node.Value, .Label = node.Value.empty() ? "Default" : node.Value });
                    for (AssetChoice& choice : offered)
                    {
                        entry.Options.push_back(std::move(choice.Path));
                        entry.OptionLabels.push_back(std::move(choice.Label));
                        entry.OptionImages.push_back(std::move(choice.Image));
                    }
                    entry.BrowseAction = Callbacks.Browse(node.Descriptor.Asset);
                    if (!entry.BrowseAction.empty())
                    {
                        entry.OptionLabels.push_back("Browse\u2026");
                        entry.OptionImages.emplace_back();
                    }
                }
                else if (node.Descriptor.Kind == PropertyKind::Toggle)
                {
                    entry.Options = { "true", "false" };
                    entry.OptionLabels = { "On", "Off" };
                }
                else
                {
                    entry.Options = node.Descriptor.Choices;
                    entry.OptionLabels = node.Descriptor.Choices;
                }
                const auto current = std::ranges::find(entry.Options, node.Value);
                entry.Current = current != entry.Options.end() ? static_cast<int>(current - entry.Options.begin()) : -1;
                Entries.push_back(std::move(entry));
                ++choices;
            });
        }
    };
    gather(selection.Sections, false);
    gather(selection.Details, true);
    // With no section to read out, the first values of the details do.
    if (Lines.size() == 1)
        for (const PropertySection& section : selection.Details)
            if (section.Source != nullptr)
                Walk(section.Source->Nodes(), [&](const PropertyNode& node) {
                    if (Lines.size() <= kSummaryLimit && ReadsOut(node))
                        Lines.push_back(node.Descriptor.Label + "  " + node.Value);
                });

    const ActionContextStack* actions = Callbacks.Actions ? Callbacks.Actions() : nullptr;
    std::vector<std::string> available;
    for (const std::string& id : actions != nullptr ? selection.Actions : std::vector<std::string>{})
    {
        const ActionContextStack::State state = actions->Query(id);
        if (state.Claimed && state.Enabled && Catalog.Find(id) != nullptr)
            available.push_back(id);
    }
    const std::size_t direct = available.size() > kActionLimit + 1 ? kActionLimit : available.size();
    for (std::size_t i = 0; i < direct; ++i)
        Entries.push_back({ .Kind = EntryKind::Action, .Label = Catalog.Find(available[i])->Label, .Action = available[i] });
    if (direct < available.size())
    {
        Entry rest{ .Kind = EntryKind::Actions, .Label = "Actions" };
        for (std::size_t i = direct; i < available.size(); ++i)
        {
            rest.Options.push_back(available[i]);
            rest.OptionLabels.push_back(Catalog.Find(available[i])->Label);
        }
        Entries.push_back(std::move(rest));
    }
    if (actions != nullptr && !Callbacks.Details.empty() && actions->Query(Callbacks.Details).Claimed)
        Entries.push_back({ .Kind = EntryKind::Action, .Label = "More…", .Action = Callbacks.Details });

    for (const Entry& entry : Entries)
    {
        std::vector<MenuItem>& fan = Fans.emplace_back();
        for (std::size_t i = 0; i < entry.OptionLabels.size(); ++i)
            fan.push_back({ .Label = entry.OptionLabels[i],
                            .Image = i < entry.OptionImages.size() ? std::string_view(entry.OptionImages[i])
                                                                   : std::string_view{} });
    }
}

IRadialMenuModel::MenuItem ContextMenuModel::Item(int index) const
{
    return index >= 0 && index < Count() ? MenuItem{ .Label = Entries[static_cast<std::size_t>(index)].Label } : MenuItem{};
}

std::span<const IRadialMenuModel::MenuItem> ContextMenuModel::Variants(int index) const
{
    if (index < 0 || index >= Count())
        return {};
    return Fans[static_cast<std::size_t>(index)];
}

int ContextMenuModel::ActiveVariant(int index) const
{
    return index >= 0 && index < Count() ? Entries[static_cast<std::size_t>(index)].Current : -1;
}

void ContextMenuModel::Select(int index, int variant)
{
    if (index < 0 || index >= Count())
        return;
    const Entry& entry = Entries[static_cast<std::size_t>(index)];
    const bool chosen = variant >= 0 && static_cast<std::size_t>(variant) < entry.Options.size();
    switch (entry.Kind)
    {
    case EntryKind::Action:
        Invoke(entry.Action);
        return;
    case EntryKind::Actions:
        if (chosen)
            Invoke(entry.Options[static_cast<std::size_t>(variant)]);
        return;
    case EntryKind::Choice:
        if (chosen && variant != entry.Current)
            Choose(entry, entry.Options[static_cast<std::size_t>(variant)]);
        return;
    case EntryKind::Asset:
        if (chosen && variant != entry.Current)
            Choose(entry, entry.Options[static_cast<std::size_t>(variant)]);
        else if (!entry.BrowseAction.empty() && static_cast<std::size_t>(variant) == entry.Options.size())
            Invoke(entry.BrowseAction);
        return;
    }
}

void ContextMenuModel::Choose(const Entry& entry, const std::string& value)
{
    if (!Callbacks.Selection)
        return;
    const WorkspaceSelectionFacet selection = Callbacks.Selection();
    const std::vector<PropertySection>& sections = entry.InDetails ? selection.Details : selection.Sections;
    if (entry.Section >= sections.size() || sections[entry.Section].Source == nullptr)
        return;
    IPropertySource& source = *sections[entry.Section].Source;
    if (source.BeginEdit(entry.Key))
        (void)source.CommitEdit(entry.Key, value);
}
