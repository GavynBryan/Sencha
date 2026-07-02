#include "ProjectCatalog.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

std::filesystem::path ProjectCatalog::DefaultCatalogPath()
{
    std::filesystem::path configDir;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0')
        configDir = xdg;
    else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
        configDir = std::filesystem::path(home) / ".config";
    else
        configDir = ".";
    return configDir / "sencha" / "recent_projects.json";
}

bool ProjectCatalog::Load(const std::filesystem::path& file, std::string* error)
{
    List.clear();

    std::error_code ec;
    if (!std::filesystem::exists(file, ec))
        return true;

    std::ifstream in(file);
    if (!in.is_open())
    {
        if (error != nullptr)
            *error = "could not open catalog '" + file.string() + "'";
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    JsonParseError parseError;
    const std::optional<JsonValue> json = JsonParse(buffer.str(), &parseError);
    if (!json || !json->IsObject())
    {
        if (error != nullptr)
            *error = "catalog JSON parse error: " + parseError.Message;
        return false;
    }

    if (const JsonValue* projects = json->Find("projects");
        projects != nullptr && projects->IsArray())
    {
        for (const JsonValue& entry : projects->AsArray())
        {
            if (!entry.IsObject())
                continue;
            const JsonValue* path = entry.Find("path");
            if (path == nullptr || !path->IsString() || path->AsString().empty())
                continue;
            const JsonValue* name = entry.Find("name");
            List.push_back(ProjectCatalogEntry{
                path->AsString(),
                (name != nullptr && name->IsString()) ? name->AsString() : std::string{},
            });
            if (List.size() >= kMaxEntries)
                break;
        }
    }
    return true;
}

bool ProjectCatalog::Save(const std::filesystem::path& file, std::string* error) const
{
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    JsonValue::Array projects;
    projects.reserve(List.size());
    for (const ProjectCatalogEntry& entry : List)
        projects.push_back(JsonValue(JsonValue::Object{
            { "path", JsonValue(entry.Path) },
            { "name", JsonValue(entry.Name) },
        }));

    JsonValue root(JsonValue::Object{
        { "projects", JsonValue(std::move(projects)) },
    });

    std::ofstream out(file, std::ios::trunc);
    if (!out.is_open())
    {
        if (error != nullptr)
            *error = "could not write catalog '" + file.string() + "'";
        return false;
    }
    out << JsonStringify(root, /*pretty*/ true);
    if (!out.good())
    {
        if (error != nullptr)
            *error = "write failed for '" + file.string() + "'";
        return false;
    }
    return true;
}

void ProjectCatalog::Touch(std::string path, std::string name)
{
    List.erase(std::remove_if(List.begin(), List.end(),
                              [&](const ProjectCatalogEntry& e) { return e.Path == path; }),
               List.end());
    List.insert(List.begin(), ProjectCatalogEntry{ std::move(path), std::move(name) });
    if (List.size() > kMaxEntries)
        List.resize(kMaxEntries);
}

void ProjectCatalog::Remove(std::string path)
{
    List.erase(std::remove_if(List.begin(), List.end(),
                              [&](const ProjectCatalogEntry& e) { return e.Path == path; }),
               List.end());
}
