#pragma once

#include <render/static_mesh/StaticMeshData.h>

#include <cstddef>
#include <span>
#include <string_view>

class BinaryReader;
class LoggingProvider;
class Logger;

class StaticMeshLoader
{
public:
    explicit StaticMeshLoader(LoggingProvider& logging);

    [[nodiscard]] bool LoadFromFile(std::string_view path, StaticMeshData& out);
    [[nodiscard]] bool LoadFromBytes(std::span<const std::byte> bytes, StaticMeshData& out);

private:
    [[nodiscard]] bool LoadFromBytes(std::span<const std::byte> bytes,
                                     StaticMeshData& out,
                                     std::string_view sourceName);
    [[nodiscard]] bool LoadFromReader(BinaryReader& reader,
                                      size_t fileSize,
                                      std::string_view sourceName,
                                      StaticMeshData& out);

    Logger& Log;
};
