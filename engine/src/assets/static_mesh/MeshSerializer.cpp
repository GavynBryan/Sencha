#include <assets/static_mesh/MeshSerializer.h>

#include <assets/static_mesh/StaticMeshFormat.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/BinaryWriter.h>
#include <render/static_mesh/MeshValidation.h>

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

MeshSerializer::MeshSerializer(LoggingProvider& logging)
    : Log(logging.GetLogger<MeshSerializer>())
{
}

namespace
{
    bool FinishFileWrite(std::ofstream& stream, Logger& log, std::string_view path, bool wroteOk)
    {
        if (!wroteOk)
            return false;
        if (!stream.good())
        {
            log.Error("MeshSerializer: failed to write '{}'", path);
            return false;
        }
        return true;
    }

    void CopyToBytes(std::ostringstream& stream, std::vector<std::byte>& out)
    {
        const std::string bytes = stream.str();
        out.resize(bytes.size());
        if (!bytes.empty())
            std::memcpy(out.data(), bytes.data(), bytes.size());
    }
}

bool MeshSerializer::WriteToFile(std::string_view path, const MeshGeometry& mesh)
{
    std::ofstream stream(std::string(path), std::ios::binary);
    if (!stream.is_open())
    {
        Log.Error("MeshSerializer: failed to open '{}' for write", path);
        return false;
    }
    BinaryWriter writer(stream);
    return FinishFileWrite(stream, Log, path, WriteToWriter(writer, mesh, nullptr));
}

bool MeshSerializer::WriteToBytes(const MeshGeometry& mesh, std::vector<std::byte>& out)
{
    std::ostringstream stream(std::ios::binary);
    BinaryWriter writer(stream);
    if (!WriteToWriter(writer, mesh, nullptr))
        return false;
    CopyToBytes(stream, out);
    return true;
}

bool MeshSerializer::WriteSkinnedToFile(std::string_view path, const SkinnedMeshData& mesh)
{
    std::ofstream stream(std::string(path), std::ios::binary);
    if (!stream.is_open())
    {
        Log.Error("MeshSerializer: failed to open '{}' for write", path);
        return false;
    }
    BinaryWriter writer(stream);
    return FinishFileWrite(stream, Log, path, WriteToWriter(writer, mesh.Geometry, &mesh.Skinning));
}

bool MeshSerializer::WriteSkinnedToBytes(const SkinnedMeshData& mesh, std::vector<std::byte>& out)
{
    std::ostringstream stream(std::ios::binary);
    BinaryWriter writer(stream);
    if (!WriteToWriter(writer, mesh.Geometry, &mesh.Skinning))
        return false;
    CopyToBytes(stream, out);
    return true;
}

bool MeshSerializer::WriteToWriter(BinaryWriter& writer,
                                   const MeshGeometry& mesh,
                                   const MeshSkinning* skinningPtr)
{
    MeshGeometry canonical = mesh;
    RecomputeMeshBounds(canonical);

    const bool skinned = skinningPtr != nullptr;
    const MeshValidationResult validation = skinned
        ? ValidateSkinnedMeshData(SkinnedMeshData{ canonical, *skinningPtr })
        : ValidateMeshGeometry(canonical);
    if (!validation.IsValid())
    {
        for (const MeshValidationError& error : validation.Errors)
            Log.Error("MeshSerializer rejected mesh: {}", error.Message);
        return false;
    }

    SmeshFileHeader header{};
    header.Magic[0] = 'S';
    header.Magic[1] = 'M';
    header.Magic[2] = 'S';
    header.Magic[3] = 'H';
    header.Version = kSmeshFormatVersion;
    header.Flags = skinned ? kSmeshFlagSkinned : 0;
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
    if (skinned)
    {
        header.JointCount = skinningPtr->JointCount;
        header.SkinningDataOffset = header.IndexDataOffset
            + static_cast<uint32_t>(sizeof(uint32_t) * canonical.Indices.size());
        header.SkeletonPathOffset = header.SkinningDataOffset
            + static_cast<uint32_t>(sizeof(MeshSkinInfluence) * skinningPtr->Influences.size());
    }

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

    if (skinned)
    {
        if (!writer.WriteBytes(
                reinterpret_cast<const char*>(skinningPtr->Influences.data()),
                static_cast<std::streamsize>(sizeof(MeshSkinInfluence)
                                             * skinningPtr->Influences.size())))
        {
            return false;
        }

        const uint32_t pathLength = static_cast<uint32_t>(skinningPtr->SkeletonPath.size());
        if (!writer.Write(pathLength)
            || !writer.WriteBytes(skinningPtr->SkeletonPath.data(),
                                  static_cast<std::streamsize>(pathLength)))
        {
            return false;
        }
    }

    return true;
}
