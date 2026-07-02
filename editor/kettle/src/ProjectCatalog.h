#pragma once

#include <filesystem>
#include <string>
#include <vector>

// One remembered project: the .senchaproj path plus a cached display name so
// the list renders without re-reading every descriptor.
struct ProjectCatalogEntry
{
    std::string Path;
    std::string Name;
};

//=============================================================================
// ProjectCatalog
//
// The launcher's recent-projects list, most recent first, persisted as JSON in
// the user config directory. Pure data + file I/O, headless-testable. Entries
// whose file has vanished are kept (the UI badges them) so a project on an
// unmounted drive is not silently forgotten.
//=============================================================================
class ProjectCatalog
{
public:
    // $XDG_CONFIG_HOME/sencha/recent_projects.json, falling back to
    // ~/.config/sencha/. Isolated here so a platform port changes one function.
    [[nodiscard]] static std::filesystem::path DefaultCatalogPath();

    // A missing file is an empty catalog (first run), not an error.
    bool Load(const std::filesystem::path& file, std::string* error = nullptr);
    bool Save(const std::filesystem::path& file, std::string* error = nullptr) const;

    // Inserts or moves the project to the front and refreshes its name. Takes
    // copies on purpose: callers routinely pass references into Entries()
    // (e.g. a clicked row's own Path), which the erase below would invalidate.
    void Touch(std::string path, std::string name);
    void Remove(std::string path);

    [[nodiscard]] const std::vector<ProjectCatalogEntry>& Entries() const { return List; }

private:
    static constexpr std::size_t kMaxEntries = 20;
    std::vector<ProjectCatalogEntry> List;
};
