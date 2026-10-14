#include "ProjectSettings.h"

#include "properties/SettingProperties.h"

#include <charconv>
#include <filesystem>
#include <format>

namespace
{
constexpr std::string_view kRootPrefix = "root:";

std::optional<std::size_t> RootIndex(std::string_view key)
{
    if (!key.starts_with(kRootPrefix))
        return std::nullopt;
    std::size_t index = 0;
    const std::string_view digits = key.substr(kRootPrefix.size());
    if (std::from_chars(digits.data(), digits.data() + digits.size(), index).ec != std::errc{})
        return std::nullopt;
    return index;
}
} // namespace

void ProjectSettings::Show(std::optional<ProjectDescriptor> descriptor, std::string problem)
{
    Descriptor = std::move(descriptor);
    Problem = std::move(problem);
}

std::vector<PropertyNode> ProjectSettings::Nodes() const
{
    std::vector<PropertyNode> nodes;
    if (!Problem.empty())
        nodes.push_back(NoteProperty("problem", Descriptor.has_value() ? "Not saved" : "Unreadable", Problem));
    if (!Descriptor.has_value())
        return nodes;
    nodes.push_back(TextProperty("name", "Name", Descriptor->Name));
    nodes.push_back(TextProperty("module", "Game module", Descriptor->GameModulePath,
                                 "The built game library the editor loads for this project"));
    PropertyNode roots = GroupProperty("roots", "Content roots");
    for (std::size_t i = 0; i < Descriptor->ContentRoots.size(); ++i)
    {
        PropertyNode root = NoteProperty(std::format("{}{}", kRootPrefix, i), Descriptor->ContentRoots[i], "");
        root.Descriptor.Verbs.push_back({ .Id = "remove", .Label = "Remove" });
        roots.Children.push_back(std::move(root));
    }
    roots.Children.push_back(TextProperty("root.add", "Add a root", "", "An absolute path, or one relative to the project"));
    nodes.push_back(std::move(roots));
    return nodes;
}

bool ProjectSettings::BeginEdit(std::string_view key)
{
    return Descriptor.has_value() && (key == "name" || key == "module" || key == "root.add");
}

PropertyEditResult ProjectSettings::CommitEdit(std::string_view key, std::string_view value)
{
    if (!Descriptor.has_value())
        return PropertyEditResult::Gone;
    ProjectDescriptor changed = *Descriptor;
    if (key == "name")
        changed.Name = std::string(value);
    else if (key == "module")
        changed.GameModulePath = std::string(value);
    else if (key == "root.add")
    {
        if (value.empty())
            return PropertyEditResult::Unchanged;
        // Roots are absolute in memory; saving writes what it can relative.
        std::filesystem::path root{ std::string(value) };
        if (root.is_relative())
            root = std::filesystem::path(changed.Directory) / root;
        changed.ContentRoots.push_back(root.lexically_normal().string());
    }
    else
        return PropertyEditResult::Gone;
    return Keep(std::move(changed));
}

PropertyEditResult ProjectSettings::Act(std::string_view key, std::string_view verb, std::string_view)
{
    const std::optional<std::size_t> index = RootIndex(key);
    if (!Descriptor.has_value() || verb != "remove" || !index.has_value() || *index >= Descriptor->ContentRoots.size())
        return PropertyEditResult::Gone;
    ProjectDescriptor changed = *Descriptor;
    changed.ContentRoots.erase(changed.ContentRoots.begin() + static_cast<std::ptrdiff_t>(*index));
    return Keep(std::move(changed));
}

PropertyEditResult ProjectSettings::Keep(ProjectDescriptor changed)
{
    std::string error;
    if (!Save || !Save(changed, error))
    {
        Problem = error.empty() ? std::string("The project file could not be written") : error;
        return PropertyEditResult::Refused;
    }
    Descriptor = std::move(changed);
    Problem.clear();
    return PropertyEditResult::Applied;
}
