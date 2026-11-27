#pragma once

#include <assets/data/DataDefinitionStaging.h>
#include <core/json/JsonValue.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

struct DataSourceRoot
{
    std::optional<JsonValue> Root;
    std::string Problem;
};

using DataRootProvider = std::function<DataSourceRoot(std::string_view path)>;

// Holds every source it read until it ends: composition points into them.
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
        return [this](std::string_view path) -> DataSourceRead {
            auto [entry, added] = Read.try_emplace(std::string(path));
            if (added)
                entry->second = Roots(path);
            const DataSourceRoot& read = entry->second;
            return { read.Root ? &*read.Root : nullptr, read.Problem };
        };
    }

private:
    const DataRootProvider& Roots;
    std::unordered_map<std::string, DataSourceRoot> Read;
};
