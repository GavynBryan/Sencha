#include "Project.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    // Resolve a path from the descriptor against the project directory unless it
    // is already absolute, so a .senchaproj is a relocatable folder.
    std::string ResolveAgainst(const std::filesystem::path& dir, const std::string& value)
    {
        std::filesystem::path p(value);
        if (p.is_absolute())
            return p.lexically_normal().string();
        return (dir / p).lexically_normal().string();
    }

    // The inverse of ResolveAgainst: store a path relative to the project dir when
    // it lives under it, so a saved descriptor stays relocatable.
    std::string RelativeTo(const std::filesystem::path& dir, const std::string& absolute)
    {
        std::error_code ec;
        const std::filesystem::path rel = std::filesystem::relative(absolute, dir, ec);
        if (ec || rel.empty() || rel.native().rfind("..", 0) == 0)
            return std::filesystem::path(absolute).generic_string();
        return rel.generic_string();
    }
}

bool ProjectDescriptor::Load(const std::string& path, ProjectDescriptor& out, std::string* error)
{
    const auto setError = [error](std::string message) {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    };

    std::ifstream file(path);
    if (!file.is_open())
        return setError("could not open project file '" + path + "'");

    std::ostringstream buf;
    buf << file.rdbuf();

    JsonParseError parseError;
    std::optional<JsonValue> json = JsonParse(buf.str(), &parseError);
    if (!json)
        return setError("project JSON parse error at " + std::to_string(parseError.Position)
                        + ": " + parseError.Message);
    if (!json->IsObject())
        return setError("project file must be a JSON object");

    const std::filesystem::path dir =
        std::filesystem::absolute(std::filesystem::path(path)).parent_path();

    out = ProjectDescriptor{};
    out.Directory = dir.lexically_normal().string();

    if (const JsonValue* name = json->Find("name"); name != nullptr && name->IsString())
        out.Name = name->AsString();
    else
        out.Name = dir.filename().string();

    const JsonValue* module = json->Find("gameModule");
    if (module == nullptr || !module->IsString())
        return setError("project file missing required string field 'gameModule'");
    out.GameModulePath = ResolveAgainst(dir, module->AsString());

    if (const JsonValue* roots = json->Find("contentRoots"); roots != nullptr && roots->IsArray())
        for (const JsonValue& entry : roots->AsArray())
            if (entry.IsString())
                out.ContentRoots.push_back(ResolveAgainst(dir, entry.AsString()));

    return true;
}

bool ProjectDescriptor::Save(const std::string& path, std::string* error)
{
    const auto setError = [error](std::string message) {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    };

    const std::filesystem::path dir =
        std::filesystem::absolute(std::filesystem::path(path)).parent_path();
    Directory = dir.lexically_normal().string();

    JsonValue::Array roots;
    roots.reserve(ContentRoots.size());
    for (const std::string& root : ContentRoots)
        roots.push_back(JsonValue(RelativeTo(dir, root)));

    JsonValue root(JsonValue::Object{
        { "name", JsonValue(Name) },
        { "gameModule", JsonValue(RelativeTo(dir, GameModulePath)) },
        { "contentRoots", JsonValue(std::move(roots)) },
    });

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open())
        return setError("could not write project file '" + path + "'");
    file << JsonStringify(root, /*pretty*/ true);
    if (!file.good())
        return setError("write failed for '" + path + "'");
    return true;
}

bool ProjectDescriptor::Create(const std::string& directory,
                               const std::string& name,
                               ProjectDescriptor& out,
                               std::string* error)
{
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    std::filesystem::create_directories(std::filesystem::path(directory) / "assets", ec);

    out = ProjectDescriptor{};
    out.Name = name.empty() ? std::filesystem::path(directory).filename().string() : name;
    out.GameModulePath = (std::filesystem::path(directory) / "build/game.so").lexically_normal().string();
    out.ContentRoots = { (std::filesystem::path(directory) / "assets").lexically_normal().string() };

    const std::string descriptorPath =
        (std::filesystem::path(directory) / "project.senchaproj").string();
    return out.Save(descriptorPath, error);
}
