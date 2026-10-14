#pragma once

#include <assets/cook/AssetImporter.h>

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// An importer's output kept in memory, by the path it was written to.
class MemoryCookOutputWriter final : public ICookOutputWriter
{
public:
    bool WriteBytes(std::string_view fileRelPath, std::span<const std::byte> bytes) override
    {
        Files[std::string(fileRelPath)].assign(bytes.begin(), bytes.end());
        return true;
    }

    std::map<std::string, std::vector<std::byte>> Files;
};
