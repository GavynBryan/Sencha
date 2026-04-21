#pragma once

#include <render/static_mesh/StaticMeshData.h>

#include <cstddef>
#include <string_view>
#include <vector>

class BinaryWriter;
class LoggingProvider;
class Logger;

class StaticMeshSerializer
{
public:
    explicit StaticMeshSerializer(LoggingProvider& logging);

    [[nodiscard]] bool WriteToFile(std::string_view path, const StaticMeshData& mesh);
    [[nodiscard]] bool WriteToBytes(const StaticMeshData& mesh, std::vector<std::byte>& out);

private:
    [[nodiscard]] bool WriteToWriter(BinaryWriter& writer, const StaticMeshData& mesh);

    Logger& Log;
};
