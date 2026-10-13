#include "data/DataCreation.h"

#include "properties/SettingProperties.h"

#include <algorithm>

void DataCreation::Clear()
{
    Subtype.clear();
    Path.clear();
    Problem.clear();
}

std::vector<PropertyNode> DataCreation::Nodes() const
{
    const std::vector<std::string> subtypes = Subtypes ? Subtypes() : std::vector<std::string>{};
    PropertyNode asset = GroupProperty("asset", "Data asset");
    asset.Descriptor.Verbs.push_back({ .Id = "create", .Label = "Create" });
    if (subtypes.empty())
    {
        asset.Children.push_back(NoteProperty("none", "Kind", "No data kind with an authoring schema is registered"));
        return { std::move(asset) };
    }
    std::vector<std::string_view> choices(subtypes.begin(), subtypes.end());
    const auto current = std::ranges::find(subtypes, Subtype);
    asset.Children.push_back(ChoiceProperty("kind", "Kind", choices,
                                            current != subtypes.end() ? static_cast<std::size_t>(current - subtypes.begin()) : 0));
    asset.Children.push_back(TextProperty("path", "Path", Path, "Relative to the content root, as input/player.profile.sdata"));
    if (!Problem.empty())
        asset.Children.push_back(NoteProperty("problem", "Not created", Problem));
    return { std::move(asset) };
}

PropertyEditResult DataCreation::CommitEdit(std::string_view key, std::string_view value)
{
    if (key == "kind")
        Subtype = std::string(value);
    else if (key == "path")
        Path = std::string(value);
    else
        return PropertyEditResult::Gone;
    return PropertyEditResult::Applied;
}

PropertyEditResult DataCreation::Act(std::string_view key, std::string_view verb, std::string_view)
{
    if (key != "asset" || verb != "create")
        return PropertyEditResult::Gone;
    const std::vector<std::string> subtypes = Subtypes ? Subtypes() : std::vector<std::string>{};
    const std::string subtype = Subtype.empty() && !subtypes.empty() ? subtypes.front() : Subtype;
    if (subtype.empty() || Path.empty())
    {
        Problem = "Say what kind it is and where it goes";
        return PropertyEditResult::Refused;
    }
    std::string error;
    if (!Create || !Create(subtype, Path, error))
    {
        Problem = error.empty() ? std::string("The asset could not be created") : error;
        return PropertyEditResult::Refused;
    }
    Clear();
    return PropertyEditResult::Applied;
}
