#pragma once

#include <assets/cook/CookDiagnostic.h>
#include <assets/cook/NavigationCook.h>
#include <assets/cook/NavigationSettings.h>

#include <filesystem>
#include <optional>
#include <vector>

class EditorDocument;

// What the navigation step builds from, collected the same way for the cook
// and for in-editor analysis.

// Every authored navigation link, anchors resolved in world space through the
// composed transform so a parented link is placed where it appears.
[[nodiscard]] std::vector<NavLinkRecord> CollectNavLinks(const EditorDocument& document);

struct NavigationSettingsLookup
{
    // Absent when the project has none, or its settings are ambiguous or invalid.
    std::optional<NavigationSettings> Settings;
    std::vector<CookDiagnostic> Diagnostics;
};

// The project's navigation settings: the single navigation.settings data asset
// under the assets root.
[[nodiscard]] NavigationSettingsLookup FindNavigationSettings(const std::filesystem::path& assetsRoot);
