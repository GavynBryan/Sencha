#include <assets/static_mesh/StaticMeshLoader.h>

#include <assets/static_mesh/StaticMeshFormat.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/BinaryReader.h>
#include <render/static_mesh/StaticMeshValidation.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <istream>
#include <vector>

namespace
{
    class MemoryStreamBuffer final : public std::streambuf
    {
    public:
        explicit MemoryStreamBuffer(std::span<const std::byte> bytes)
        {
            char* begin = &Empty;
            char* end = begin;
            if (!bytes.empty())
            {
                begin = reinterpret_cast<char*>(const_cast<std::byte*>(bytes.data()));
                end = begin + bytes.size();
            }

            setg(begin, begin, end);
        }

    protected:
        std::streambuf* setbuf(char_type*, std::streamsize) override
        {
            return this;
        }

        std::streamsize xsgetn(char_type* destination, std::streamsize count) override
        {
            const std::streamsize available = egptr() - gptr();
            const std::streamsize toRead = std::min(count, available);
            if (toRead > 0)
            {
                std::memcpy(destination, gptr(), static_cast<size_t>(toRead));
                gbump(static_cast<int>(toRead));
            }
            return toRead;
        }

        pos_type seekoff(off_type offset,
                         std::ios_base::seekdir direction,
                         std::ios_base::openmode which) override
        {
            if ((which & std::ios_base::in) == 0)
                return pos_type(off_type(-1));

            const off_type size = egptr() - eback();
            off_type next = 0;
            switch (direction)
            {
            case std::ios_base::beg:
                next = offset;
                break;
            case std::ios_base::cur:
                next = (gptr() - eback()) + offset;
                break;
            case std::ios_base::end:
                next = size + offset;
                break;
            default:
                return pos_type(off_type(-1));
            }

            if (next < 0 || next > size)
                return pos_type(off_type(-1));

            setg(eback(), eback() + next, egptr());
            return pos_type(next);
        }

        pos_type seekpos(pos_type position, std::ios_base::openmode which) override
        {
            return seekoff(off_type(position), std::ios_base::beg, which);
        }

    private:
        char Empty = '\0';
    };

    struct ByteRegion
    {
        uint64_t Offset = 0;
        uint64_t Size = 0;

        [[nodiscard]] uint64_t End() const
        {
            return Offset + Size;
        }
    };

    Aabb3d ReadBounds(const float (&minValue)[3], const float (&maxValue)[3])
    {
        return Aabb3d::FromMinMax(
            Vec3d(minValue[0], minValue[1], minValue[2]),
            Vec3d(maxValue[0], maxValue[1], maxValue[2]));
    }

    template <typename T>
    bool ReadObjectAt(BinaryReader& reader, size_t offset, T& out)
    {
        std::istream& stream = reader.GetStream();
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!stream.good())
            return false;

        return reader.Read(out);
    }

    template <typename T>
    bool ReadArrayAt(BinaryReader& reader, size_t offset, size_t count, std::vector<T>& out)
    {
        std::istream& stream = reader.GetStream();
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!stream.good())
            return false;

        out.resize(count);
        if (count == 0)
            return true;

        return reader.ReadBytes(
            reinterpret_cast<char*>(out.data()),
            static_cast<std::streamsize>(sizeof(T) * count));
    }

    bool IsRegionWithinFile(const ByteRegion& region, size_t fileSize)
    {
        return region.End() >= region.Offset && region.End() <= fileSize;
    }

    bool RegionsOverlap(const ByteRegion& a, const ByteRegion& b)
    {
        return a.Offset < b.End() && b.Offset < a.End();
    }
}

StaticMeshLoader::StaticMeshLoader(LoggingProvider& logging)
    : Log(logging.GetLogger<StaticMeshLoader>())
{
}

bool StaticMeshLoader::LoadFromFile(std::string_view path, StaticMeshData& out)
{
    std::ifstream stream(std::string(path), std::ios::binary);
    if (!stream.is_open())
    {
        Log.Error("StaticMeshLoader: failed to load '{}': could not open file", path);
        return false;
    }

    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size < 0)
    {
        Log.Error("StaticMeshLoader: failed to load '{}': invalid file size", path);
        return false;
    }

    stream.seekg(0, std::ios::beg);
    BinaryReader reader(stream);
    return LoadFromReader(reader, static_cast<size_t>(size), path, out);
}

bool StaticMeshLoader::LoadFromBytes(std::span<const std::byte> bytes, StaticMeshData& out)
{
    return LoadFromBytes(bytes, out, "<memory>");
}

bool StaticMeshLoader::LoadFromBytes(std::span<const std::byte> bytes,
                                     StaticMeshData& out,
                                     std::string_view sourceName)
{
    MemoryStreamBuffer buffer(bytes);
    std::istream stream(&buffer);
    BinaryReader reader(stream);
    return LoadFromReader(reader, bytes.size(), sourceName, out);
}

bool StaticMeshLoader::LoadFromReader(BinaryReader& reader,
                                      size_t fileSize,
                                      std::string_view sourceName,
                                      StaticMeshData& out)
{
    out = {};

    if (fileSize < sizeof(SmeshFileHeader))
    {
        Log.Error("StaticMeshLoader: failed to load '{}': file too small", sourceName);
        return false;
    }

    SmeshFileHeader header{};
    if (!ReadObjectAt(reader, 0, header))
    {
        Log.Error("StaticMeshLoader: failed to load '{}': could not read header", sourceName);
        return false;
    }

    if (std::memcmp(header.Magic, "SMSH", 4) != 0)
    {
        Log.Error("StaticMeshLoader: failed to load '{}': invalid magic", sourceName);
        return false;
    }
    if (header.Version != 1)
    {
        Log.Error("StaticMeshLoader: failed to load '{}': unsupported version {}", sourceName, header.Version);
        return false;
    }
    if (header.Flags != 0 || header.Reserved0 != 0 || header.Reserved1 != 0 || header.Reserved2 != 0)
    {
        Log.Error("StaticMeshLoader: failed to load '{}': unsupported flags or reserved fields", sourceName);
        return false;
    }
    if (header.HeaderSize != sizeof(SmeshFileHeader))
    {
        Log.Error("StaticMeshLoader: failed to load '{}': header size mismatch", sourceName);
        return false;
    }
    if (header.VertexStride != sizeof(StaticMeshVertex))
    {
        Log.Error("StaticMeshLoader: failed to load '{}': vertex stride mismatch", sourceName);
        return false;
    }
    if (header.IndexFormat != SmeshIndexFormat::UInt32)
    {
        Log.Error("StaticMeshLoader: failed to load '{}': unsupported index format", sourceName);
        return false;
    }
    if (header.Topology != SmeshTopology::TriangleList)
    {
        Log.Error("StaticMeshLoader: failed to load '{}': unsupported topology", sourceName);
        return false;
    }
    if (header.VertexCount == 0 || header.IndexCount == 0 || header.SectionCount == 0)
    {
        Log.Error("StaticMeshLoader: failed to load '{}': vertex/index/section counts must be nonzero", sourceName);
        return false;
    }

    const uint64_t sectionBytes = uint64_t(sizeof(SmeshSectionRecord)) * header.SectionCount;
    const uint64_t vertexBytes = uint64_t(sizeof(StaticMeshVertex)) * header.VertexCount;
    const uint64_t indexBytes = uint64_t(sizeof(uint32_t)) * header.IndexCount;

    const ByteRegion sections{
        .Offset = header.SectionTableOffset,
        .Size = sectionBytes,
    };
    const ByteRegion vertices{
        .Offset = header.VertexDataOffset,
        .Size = vertexBytes,
    };
    const ByteRegion indices{
        .Offset = header.IndexDataOffset,
        .Size = indexBytes,
    };

    if (!IsRegionWithinFile(sections, fileSize)
        || !IsRegionWithinFile(vertices, fileSize)
        || !IsRegionWithinFile(indices, fileSize)
        || RegionsOverlap(sections, vertices)
        || RegionsOverlap(sections, indices)
        || RegionsOverlap(vertices, indices))
    {
        Log.Error("StaticMeshLoader: failed to load '{}': invalid offsets", sourceName);
        return false;
    }

    std::vector<SmeshSectionRecord> records;
    if (!ReadArrayAt(reader, header.SectionTableOffset, header.SectionCount, records))
    {
        Log.Error("StaticMeshLoader: failed to load '{}': could not read section table", sourceName);
        return false;
    }
    if (!ReadArrayAt(reader, header.VertexDataOffset, header.VertexCount, out.Vertices))
    {
        Log.Error("StaticMeshLoader: failed to load '{}': could not read vertex data", sourceName);
        return false;
    }
    if (!ReadArrayAt(reader, header.IndexDataOffset, header.IndexCount, out.Indices))
    {
        Log.Error("StaticMeshLoader: failed to load '{}': could not read index data", sourceName);
        return false;
    }

    out.LocalBounds = ReadBounds(header.BoundsMin, header.BoundsMax);
    out.Sections.reserve(records.size());
    for (size_t sectionIndex = 0; sectionIndex < records.size(); ++sectionIndex)
    {
        const SmeshSectionRecord& record = records[sectionIndex];
        if (record.Reserved0 != 0)
        {
            Log.Error("StaticMeshLoader: failed to load '{}': section {} reserved field must be zero",
                      sourceName, sectionIndex);
            out = {};
            return false;
        }

        StaticMeshSection section;
        section.IndexOffset = record.IndexOffset;
        section.IndexCount = record.IndexCount;
        section.VertexOffset = record.VertexOffset;
        section.VertexCount = record.VertexCount;
        section.MaterialSlot = record.MaterialSlot;
        section.LocalBounds = ReadBounds(record.BoundsMin, record.BoundsMax);
        out.Sections.push_back(section);
    }

    const StaticMeshValidationResult validation = ValidateStaticMeshData(out);
    if (!validation.IsValid())
    {
        for (const StaticMeshValidationError& error : validation.Errors)
            Log.Error("StaticMeshLoader: failed to load '{}': {}", sourceName, error.Message);
        out = {};
        return false;
    }

    return true;
}
