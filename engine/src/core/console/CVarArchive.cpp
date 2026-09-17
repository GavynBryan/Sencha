#include <core/console/CVarArchive.h>

#include <core/console/ConsoleRegistry.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>

#include <cstdint>
#include <fstream>
#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <cctype>

namespace
{
    // The section name engine.json already uses for the same thing, so the two
    // files are visibly one idea and an entry can be moved between them.
    constexpr const char* kSection = "cvars";

    const char* TypeName(CVarType type)
    {
        switch (type)
        {
        case CVarType::Bool:   return "boolean";
        case CVarType::Int:    return "whole number";
        case CVarType::Double: return "number";
        case CVarType::String: return "string";
        }
        return "value";
    }

    ConsoleValueSource ArchiveSource(const std::filesystem::path& path)
    {
        ConsoleValueSource source;
        source.Description = "saved settings";
        source.File = path.string();
        return source;
    }

    // A saved scalar as the text the console's own parser expects. Only used
    // for a name no cvar claims yet, where there is no declared type to convert
    // against; a registered cvar takes the typed path below instead.
    bool ScalarToText(const JsonValue& value, std::string& out)
    {
        if (value.IsBool())
        {
            out = value.AsBool() ? "true" : "false";
            return true;
        }
        if (value.IsString())
        {
            out = value.AsString();
            return true;
        }
        if (value.IsNumber())
        {
            // Through the JSON writer rather than a stream, so the text carries
            // the same shortest-round-trip form the file does.
            out = JsonStringify(JsonValue(value.AsNumber()));
            return true;
        }
        return false;
    }

    // A saved scalar as the cvar's declared type. Nullopt when the file holds a
    // shape the cvar cannot take -- a string where a number belongs, say --
    // which is a diagnostic rather than a silent default.
    std::optional<CVarValue> ScalarToValue(const JsonValue& value, CVarType type)
    {
        switch (type)
        {
        case CVarType::Bool:
            if (value.IsBool()) return CVarValue{ value.AsBool() };
            return std::nullopt;
        case CVarType::Int:
            if (value.IsNumber())
                return CVarValue{ static_cast<std::int64_t>(value.AsNumber()) };
            return std::nullopt;
        case CVarType::Double:
            if (value.IsNumber()) return CVarValue{ value.AsNumber() };
            return std::nullopt;
        case CVarType::String:
            if (value.IsString()) return CVarValue{ value.AsString() };
            return std::nullopt;
        }
        return std::nullopt;
    }

    JsonValue ValueToJson(const CVarValue& value)
    {
        return std::visit([](const auto& v) -> JsonValue {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>)
                return JsonValue(v);
            else if constexpr (std::is_same_v<T, std::string>)
                return JsonValue(v);
            else
                return JsonValue(static_cast<double>(v));
        }, value);
    }
}

std::filesystem::path CVarArchive::FileFor(const std::filesystem::path& root, std::string_view appName)
{
    std::string slug;
    slug.reserve(appName.size());
    for (unsigned char c : appName)
    {
        if (std::isalnum(c) != 0)
            slug.push_back(static_cast<char>(std::tolower(c)));
        else if (!slug.empty() && slug.back() != '-')
            slug.push_back('-');
    }
    while (!slug.empty() && slug.back() == '-')
        slug.pop_back();
    if (slug.empty())
        slug = "sencha-application";
    return root / slug / "settings.json";
}

CVarArchive::CVarArchive(std::filesystem::path file)
    : Path(std::move(file))
{
}

void CVarArchive::Load(ConsoleRegistry& registry, ConsolePhase phase)
{
    Diagnostics.clear();

    std::error_code ec;
    if (!std::filesystem::exists(Path, ec) || ec)
    {
        // No settings saved yet. The ordinary first run, not a failure.
        SyncedRevision = registry.ArchiveRevision();
        return;
    }

    std::string parseError;
    std::optional<JsonValue> root = JsonParseFile(Path, &parseError);
    if (!root)
    {
        Diagnostics.push_back("settings file could not be read: " + parseError);
        SyncedRevision = registry.ArchiveRevision();
        return;
    }

    const JsonValue* section = root->Find(kSection);
    if (section == nullptr || !section->IsObject())
    {
        Diagnostics.push_back(std::string("settings file has no '") + kSection + "' object");
        SyncedRevision = registry.ArchiveRevision();
        return;
    }

    const ConsoleValueSource source = ArchiveSource(Path);
    for (const auto& [key, saved] : section->AsObject())
    {
        const std::string name = CanonicalConsoleName(key);
        if (!IsValidConsoleName(name))
        {
            Diagnostics.push_back("settings: invalid name '" + key + "'");
            continue;
        }

        const CVarMetadata* metadata = registry.FindCVar(name);
        if (metadata == nullptr)
        {
            // The cvar may belong to a module that has not registered yet. The
            // registry already keeps unresolved assignments for exactly this and
            // applies one the moment its cvar appears.
            std::string text;
            if (!ScalarToText(saved, text))
            {
                Diagnostics.push_back("settings: '" + name + "' is not a scalar");
                continue;
            }
            registry.QueuePendingAssignment(name, std::move(text), source);
            continue;
        }

        const std::optional<CVarValue> value = ScalarToValue(saved, metadata->Type);
        if (!value)
        {
            Diagnostics.push_back("settings: '" + name + "' does not hold a "
                                  + TypeName(metadata->Type));
            continue;
        }

        const ConsoleResult result = registry.SetCVar(name, *value, source, phase);
        if (!result.Succeeded())
        {
            Diagnostics.push_back("settings: '" + name + "' refused: "
                                  + (result.Output.empty() ? std::string("no reason given")
                                                           : result.Output.front().Text));
        }
    }

    // Applying saved values moved the counter; this is what stops the first
    // Save from rewriting a file it just read.
    SyncedRevision = registry.ArchiveRevision();
}

bool CVarArchive::IsDirty(const ConsoleRegistry& registry) const
{
    return registry.ArchiveRevision() != SyncedRevision;
}

bool CVarArchive::Save(const ConsoleRegistry& registry)
{
    if (!IsDirty(registry))
        return false;

    // Sorted by name by ListCVars, so the file is stable between runs and a
    // diff of it says what the player changed.
    JsonValue::Object entries;
    for (const CVarMetadata* metadata : registry.ListCVars({}, /*includeHidden*/ true))
    {
        if (!HasFlag(metadata->Flags, CVarFlags::Archive))
            continue;
        // A setting left at its default is not a setting. Writing it would bake
        // today's default into the file and stop a future engine from changing
        // one for players who never touched it.
        if (metadata->CurrentValue == metadata->DefaultValue)
            continue;
        entries.emplace_back(metadata->Name, ValueToJson(metadata->CurrentValue));
    }

    JsonValue::Object root;
    root.emplace_back(kSection, JsonValue(std::move(entries)));

    std::error_code ec;
    if (Path.has_parent_path())
    {
        std::filesystem::create_directories(Path.parent_path(), ec);
        if (ec)
            return false;
    }

    std::ofstream out(Path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << JsonStringify(JsonValue(std::move(root)), /*pretty*/ true) << '\n';
    if (!out)
        return false;
    out.close();

    SyncedRevision = registry.ArchiveRevision();
    return true;
}
