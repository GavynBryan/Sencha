#include "NavigationCookInput.h"

#include "EditorDocument.h"
#include "EditorScene.h"

#include "project/ProjectFiles.h"

#include <core/json/JsonParser.h>
#include <navigation/NavLinkComponent.h>
#include <world/registry/Registry.h>

#include <algorithm>
#include <string>
#include <system_error>
#include <utility>

std::vector<NavLinkRecord> CollectNavLinks(const EditorDocument& document)
{
    std::vector<NavLinkRecord> links;
    const World& world = document.GetRegistry().Components;
    if (!world.IsRegistered<NavLink>())
        return links;
    world.ForEachComponent<NavLink>(
        [&](EntityId entity, const NavLink& link)
        {
            const Transform3f toWorld = document.GetScene().ComposeWorldTransform(entity);
            NavLinkRecord cooked;
            cooked.Id = link.Id;
            cooked.Traversal = std::string(link.Traversal.View());
            cooked.Directions = link.Directions;
            cooked.BaseCost = link.BaseCost;
            cooked.EntryRadius = link.EntryRadius;
            cooked.Entry = toWorld.Position;
            cooked.Exit = toWorld.TransformPoint(link.ExitOffset);
            links.push_back(std::move(cooked));
        });
    return links;
}

// Files come sorted by asset path, so the "more than one" diagnostic names the
// same files every time.
NavigationSettingsLookup FindNavigationSettings(const std::filesystem::path& assetsRoot)
{
    NavigationSettingsLookup lookup;
    const std::filesystem::path roots[] = { assetsRoot };
    std::vector<std::pair<std::string, JsonValue>> found;
    for (const ProjectFile& file : ScanProjectFiles(roots))
    {
        if (file.Subtype != kNavigationSettingsSubtype)
            continue;
        const std::optional<std::string> text = ReadProjectFileText(file.FilePath);
        std::optional<JsonValue> parsed = text ? JsonParse(*text) : std::nullopt;
        if (parsed)
            found.emplace_back(file.AssetPath, std::move(*parsed));
    }

    if (found.empty())
        return lookup;
    if (found.size() > 1)
    {
        std::string message = "more than one navigation.settings asset:";
        for (const auto& [path, envelope] : found)
            message += " " + path;
        lookup.Diagnostics.push_back(CookDiagnostic{
            .Severity = CookDiagnosticSeverity::Error,
            .Source = CookDiagnosticSource::NavigationSettings,
            .Rule = "nav.settings.ambiguous",
            .Message = std::move(message),
        });
        return lookup;
    }
    NavigationSettings settings;
    if (ParseNavigationSettings(found.front().second, settings, lookup.Diagnostics))
        lookup.Settings = std::move(settings);
    return lookup;
}
