#pragma once

#include <assets/cook/CookDiagnostic.h>
#include <navigation/NavigationFile.h>

#include <filesystem>
#include <optional>
#include <vector>

class EditorDocument;

struct DocumentNavigation
{
    // Absent when the project has no navigation settings, the level has no
    // geometry, or the build reported an error.
    std::optional<NavigationFile> File;
    std::vector<CookDiagnostic> Diagnostics;
};

// The document's navigation, built in memory from the inputs the cook's
// navigation step reads, with the settings found under `assetsRoot`. Writes
// nothing.
[[nodiscard]] DocumentNavigation BuildDocumentNavigation(const EditorDocument& document,
                                                         const std::filesystem::path& assetsRoot,
                                                         double cellSize = 16.0);
