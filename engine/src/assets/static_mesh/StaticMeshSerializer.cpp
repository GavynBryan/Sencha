#include <assets/static_mesh/StaticMeshSerializer.h>

#include <assets/static_mesh/StaticMeshFormat.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/BinaryWriter.h>
#include <render/static_mesh/StaticMeshValidation.h>

#include <cstring>
#include <fstream>
#include <sstream>

namespace
{
    void WriteBounds(const Aabb3d& bounds, float (&minOut)[3], float (&maxOut)[3])
    {
        minOut[0] = static_cast<float>(bounds.Min.X);
        minOut[1] = static_cast<float>(bounds.Min.Y);
        minOut[2] = static_cast<float>(bounds.Min.Z);
        maxOut[0] = static_cast<float>(bounds.Max.X);
        maxOut[1] = static_cast<float>(bounds.Max.Y);
        maxOut[2] = static_cast<float>(bounds.Max.Z);
    }
}

StaticMeshSerializer::StaticMeshSerializer(LoggingProvider& logging)
    : Log(logging.GetLogger<StaticMeshSerializer>())
{
}

bool StaticMeshSerializer::WriteToFile(std::string_view path, const StaticMeshData& mesh)
{
    std::ofstream stream(std::string(path), std::ios::binary);
    if (!stream.is_open())
    {
        Log.Error("StaticMeshSerializer: failed to open '{}' for write", path);
        return false;
    }

    BinaryWriter writer(stream);
    if (!WriteToWriter(writer, mesh))
        return false;

    if (!stream.good())
    {
        Log.Error("StaticMeshSerializer: failed to write '{}'", path);
        return false;
    }

    return true;
}

bool StaticMeshSerializer::WriteToBytes(const StaticMeshData& mesh, std::vector<std::byte>& out)
{
    std::ostringstream stream(std::ios::binary);
    BinaryWriter writer(stream);
    if (!WriteToWriter(writer, mesh))
        return false;

    const std::string bytes = stream.str();
    out.resize(bytes.size());
    if (!bytes.empty())
        std::memcpy(out.data(), bytes.data(), bytes.size());
    return true;
}

bool StaticMeshSerializer::WriteToWriter(BinaryWriter& writer, const StaticMeshData& mesh)
{
    StaticMeshData canonical = mesh;
    RecomputeStaticMeshBounds(canonical);

    const StaticMeshValidationResult validation = ValidateStaticMeshData(canonical);
    if (!validation.IsValid())
    {
        for (const StaticMeshValidationError& error : validation.Errors)
            Log.Error("StaticMeshSerializer rejected mesh: {}", error.Message);
        return false;
    }

    SmeshFileHeader header{};
    header.Magic[0] = 'S';
    header.Magic[1] = 'M';
    header.Magic[2] = 'S';
    header.Magic[3] = 'H';
    header.Version = 1;
    header.Flags = 0;
    header.Reserved0 = 0;
    header.Reserved1 = 0;
    header.Reserved2 = 0;
    header.VertexCount = static_cast<uint32_t>(canonical.Vertices.size());
    header.IndexCount = static_cast<uint32_t>(canonical.Indices.size());
    header.SectionCount = static_cast<uint32_t>(canonical.Sections.size());
    header.VertexStride = sizeof(StaticMeshVertex);
    header.IndexFormat = SmeshIndexFormat::UInt32;
    header.Topology = SmeshTopology::TriangleList;
    WriteBounds(canonical.LocalBounds, header.BoundsMin, header.BoundsMax);
    header.HeaderSize = sizeof(SmeshFileHeader);
    header.SectionTableOffset = header.HeaderSize;
    header.VertexDataOffset = header.SectionTableOffset
        + static_cast<uint32_t>(sizeof(SmeshSectionRecord) * canonical.Sections.size());
    header.IndexDataOffset = header.VertexDataOffset
        + static_cast<uint32_t>(sizeof(StaticMeshVertex) * canonical.Vertices.size());

    if (!writer.Write(header))
        return false;

    for (const StaticMeshSection& section : canonical.Sections)
    {
        SmeshSectionRecord record{};
        record.IndexOffset = section.IndexOffset;
        record.IndexCount = section.IndexCount;
        record.VertexOffset = section.VertexOffset;
        record.VertexCount = section.VertexCount;
        record.MaterialSlot = section.MaterialSlot;
        record.Reserved0 = 0;
        WriteBounds(section.LocalBounds, record.BoundsMin, record.BoundsMax);

        if (!writer.Write(record))
            return false;
    }

    if (!canonical.Vertices.empty()
        && !writer.WriteBytes(
            reinterpret_cast<const char*>(canonical.Vertices.data()),
            static_cast<std::streamsize>(sizeof(StaticMeshVertex) * canonical.Vertices.size())))
    {
        return false;
    }

    if (!canonical.Indices.empty()
        && !writer.WriteBytes(
            reinterpret_cast<const char*>(canonical.Indices.data()),
            static_cast<std::streamsize>(sizeof(uint32_t) * canonical.Indices.size())))
    {
        return false;
    }

    return true;
}
