#include "Project.h"

#include <core/json/JsonParser.h>
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
