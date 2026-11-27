#include "ProjectSceneUpgrade.h"

#include "scene_source/SceneSourceUpgrade.h"

#include <core/io/FileBytes.h>

#include <algorithm>
#include <optional>
#include <ostream>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    [[nodiscard]] bool IsSkipped(const std::filesystem::path& directory)
    {
        const std::string name = directory.filename().string();
        std::error_code ignored;
        return (!name.empty() && name.front() == '.') || std::filesystem::exists(directory / "CMakeCache.txt", ignored);
    }

    [[nodiscard]] std::vector<std::filesystem::path> ScenesBeneath(const std::filesystem::path& root, std::error_code& error)
    {
        std::vector<std::filesystem::path> scenes;
        for (std::filesystem::recursive_directory_iterator entry(
                 root, std::filesystem::directory_options::skip_permission_denied, error);
             !error && entry != std::filesystem::recursive_directory_iterator(); entry.increment(error))
        {
            std::error_code ignored;
            if (entry->is_directory(ignored) && IsSkipped(entry->path()))
                entry.disable_recursion_pending();
            else if (entry->is_regular_file(ignored) && entry->path().extension() == ".sscene")
                scenes.push_back(entry->path());
        }
        std::ranges::sort(scenes);
        return scenes;
    }

    [[nodiscard]] std::string Joined(const std::vector<std::string>& lines)
    {
        std::string out;
        for (const std::string& line : lines)
            out += (out.empty() ? "" : "; ") + line;
        return out;
    }
}

ProjectSceneTally UpgradeProjectScenes(const std::filesystem::path& root, ProjectSceneUpgradeMode mode, std::ostream& out)
{
    ProjectSceneTally tally;
    std::error_code walkError;
    const std::vector<std::filesystem::path> scenes = ScenesBeneath(root, walkError);
    if (walkError)
    {
        out << "refused " << root.generic_string() << ": " << walkError.message() << "\n";
        ++tally.Refused;
    }

    for (const std::filesystem::path& scene : scenes)
    {
        const std::string shown = scene.lexically_relative(root).generic_string();
        const auto refuse = [&](const std::string& why) {
            out << "refused " << shown << ": " << why << "\n";
            ++tally.Refused;
        };
        std::vector<std::byte> bytes;
        if (!ReadFileBytes(scene, bytes))
        {
            refuse("could not read the file");
            continue;
        }
        std::string error;
        const std::optional<SceneSourceUpgrade> upgrade =
            UpgradeSceneSourceText(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), &error);
        if (!upgrade.has_value())
            refuse(error);
        else if (!upgrade->Refused.empty())
            refuse(Joined(upgrade->Refused));
        else if (upgrade->Changed.empty())
        {
            ++tally.Current;
            if (mode == ProjectSceneUpgradeMode::Write)
                out << "current " << shown << "\n";
        }
        else if (mode == ProjectSceneUpgradeMode::Check)
        {
            ++tally.Old;
            out << "old " << shown << ": " << Joined(upgrade->Changed) << "\n";
        }
        else if (!WriteFileText(scene, upgrade->Text))
            refuse("could not write the file");
        else
        {
            ++tally.Upgraded;
            out << "upgraded " << shown << ": " << Joined(upgrade->Changed) << "\n";
        }
    }

    out << scenes.size() << " scenes: ";
    if (mode == ProjectSceneUpgradeMode::Write)
        out << tally.Upgraded << " upgraded, ";
    out << tally.Current << " current, ";
    if (mode == ProjectSceneUpgradeMode::Check)
        out << tally.Old << " to upgrade, ";
    out << tally.Refused << " refused\n";
    return tally;
}
