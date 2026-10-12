#include "NavigationCookInput.h"

#include "EditorDocument.h"
#include "EditorScene.h"

#include <core/json/JsonParser.h>
#include <navigation/NavLinkComponent.h>
#include <world/registry/Registry.h>

#include <algorithm>
#include <fstream>
#include <sstream>
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

// Paths are visited in sorted order so the "more than one" diagnostic names the
// same files every time.
NavigationSettingsLookup FindNavigationSettings(const std::filesystem::path& assetsRoot)
{
    NavigationSettingsLookup lookup;
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(assetsRoot, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
    {
        const std::filesystem::path& path = it->path();
        if (it->is_directory() && path.filename().string().starts_with("."))
        {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file() && path.extension() == ".sdata")
            candidates.push_back(path);
    }
    std::sort(candidates.begin(), candidates.end());

    std::vector<std::pair<std::filesystem::path, JsonValue>> found;
    for (const std::filesystem::path& path : candidates)
    {
        std::ifstream stream(path, std::ios::binary);
        std::stringstream text;
        text << stream.rdbuf();
        std::optional<JsonValue> parsed = JsonParse(text.str());
        const JsonValue* type = parsed ? parsed->Find("type") : nullptr;
        if (type != nullptr && type->IsString() && type->AsString() == kNavigationSettingsSubtype)
            found.emplace_back(path, std::move(*parsed));
    }

    if (found.empty())
        return lookup;
    if (found.size() > 1)
    {
        std::string message = "more than one navigation.settings asset:";
        for (const auto& [path, envelope] : found)
            message += " " + std::filesystem::relative(path, assetsRoot, ec).generic_string();
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
