#pragma once

#include <assets/data/DataDefinitionStaging.h>
#include <core/json/JsonValue.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

// A data definition's current root by asset path: an open document's working
// copy, else its file.
using DataRootProvider = std::function<std::optional<JsonValue>(std::string_view path)>;

// One staging's view of its sources: each read once and held until the scope
// ends, so the definitions composition holds stay put while it composes.
class DataSourceScope
{
public:
    explicit DataSourceScope(const DataRootProvider& roots)
        : Roots(roots)
    {
    }

    DataSourceScope(const DataSourceScope&) = delete;
    DataSourceScope& operator=(const DataSourceScope&) = delete;

    [[nodiscard]] DataSourceReader Reader()
    {
        if (!Roots)
            return {};
        return [this](std::string_view path) -> const JsonValue* {
            auto [entry, added] = Read.try_emplace(std::string(path));
            if (added)
                entry->second = Roots(path);
            return entry->second ? &*entry->second : nullptr;
        };
    }

private:
    const DataRootProvider& Roots;
    std::unordered_map<std::string, std::optional<JsonValue>> Read;
};
