#include <assets/ui/UiPackageSerializer.h>

#include <assets/ui/UiPackageFormat.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>

namespace
{
void SetError(std::string* error, const char* message)
{
    if (error != nullptr)
        *error = message;
}

void AppendRaw(std::vector<std::byte>& out, const void* bytes, std::size_t count)
{
    const auto* first = static_cast<const std::byte*>(bytes);
    out.insert(out.end(), first, first + count);
}

template <typename T>
void AppendPod(std::vector<std::byte>& out, const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    AppendRaw(out, &value, sizeof(T));
}

// Length-prefixed, so a reader never has to guess where a field ends. u32 is
// the prefix everywhere in this container, including the blob payloads: a
// single authored document over 4 GiB is not a case worth carrying eight bytes
// per record for.
void AppendString(std::vector<std::byte>& out, const std::string& value)
{
    AppendPod(out, static_cast<std::uint32_t>(value.size()));
    AppendRaw(out, value.data(), value.size());
}

void AppendBytes(std::vector<std::byte>& out, const std::vector<std::byte>& value)
{
    AppendPod(out, static_cast<std::uint32_t>(value.size()));
    AppendRaw(out, value.data(), value.size());
}

// Sequential cursor over the container. Every read is bounds-checked against
// what remains, so a truncated or lying record fails the parse instead of
// walking off the buffer.
class Cursor
{
public:
    explicit Cursor(std::span<const std::byte> bytes) : Bytes(bytes) {}

    [[nodiscard]] bool Raw(void* out, std::size_t count)
    {
        if (count > Bytes.size() - Offset)
            return false;
        std::memcpy(out, Bytes.data() + Offset, count);
        Offset += count;
        return true;
    }

    template <typename T>
    [[nodiscard]] bool Pod(T& out)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        return Raw(&out, sizeof(T));
    }

    [[nodiscard]] bool String(std::string& out)
    {
        std::uint32_t length = 0;
        if (!Pod(length))
            return false;
        if (length > Bytes.size() - Offset)
            return false;
        out.assign(reinterpret_cast<const char*>(Bytes.data() + Offset), length);
        Offset += length;
        return true;
    }

    [[nodiscard]] bool Blob(std::vector<std::byte>& out)
    {
        std::uint32_t length = 0;
        if (!Pod(length))
            return false;
        if (length > Bytes.size() - Offset)
            return false;
        out.assign(Bytes.begin() + static_cast<std::ptrdiff_t>(Offset),
                   Bytes.begin() + static_cast<std::ptrdiff_t>(Offset + length));
        Offset += length;
        return true;
    }

    void SeekTo(std::size_t offset) { Offset = std::min(offset, Bytes.size()); }

private:
    std::span<const std::byte> Bytes;
    std::size_t Offset = 0;
};

[[nodiscard]] bool FitsU32(std::size_t value)
{
    return value <= std::numeric_limits<std::uint32_t>::max();
}
} // namespace

std::string NormalizeUiBlobName(std::string_view name)
{
    std::string normalized = std::filesystem::path(name).lexically_normal().generic_string();
    while (normalized.starts_with("./"))
        normalized.erase(0, 2);
    while (normalized.starts_with('/'))
        normalized.erase(0, 1);
    return normalized;
}

const UiPackageBlob* UiPackage::FindBlob(std::string_view virtualName) const
{
    const std::string wanted = NormalizeUiBlobName(virtualName);
    const auto it = std::find_if(Blobs.begin(), Blobs.end(),
        [&wanted](const UiPackageBlob& blob)
        { return NormalizeUiBlobName(blob.VirtualName) == wanted; });
    return it != Blobs.end() ? &*it : nullptr;
}

bool WriteSuiToBytes(const UiPackage& package, std::vector<std::byte>& out)
{
    if (!package.IsValid())
        return false;
    // A root name naming no blob would parse cleanly and then fail to open,
    // which is a worse failure than refusing to write it.
    if (package.FindBlob(package.RootDocumentName) == nullptr)
        return false;
    if (!FitsU32(package.Blobs.size()) || !FitsU32(package.Resources.size())
        || !FitsU32(package.Unsupported.size()))
        return false;
    for (const UiPackageBlob& blob : package.Blobs)
    {
        if (!FitsU32(blob.Bytes.size()) || !FitsU32(blob.VirtualName.size())
            || !FitsU32(blob.SourcePath.size()))
            return false;
    }

    SuiFileHeader header{};
    std::memcpy(header.Magic, kSuiMagic, sizeof(header.Magic));
    header.Version = kSuiVersion;
    header.Flags = kSuiFlagNone;
    header.HeaderSize = sizeof(SuiFileHeader);
    header.BlobCount = static_cast<std::uint32_t>(package.Blobs.size());
    header.ResourceCount = static_cast<std::uint32_t>(package.Resources.size());
    header.UnsupportedCount = static_cast<std::uint32_t>(package.Unsupported.size());

    out.clear();
    AppendPod(out, header);
    AppendString(out, package.RootDocumentName);

    for (const UiPackageBlob& blob : package.Blobs)
    {
        AppendString(out, blob.VirtualName);
        AppendString(out, blob.SourcePath);
        AppendPod(out, static_cast<std::uint16_t>(blob.Kind));
        AppendBytes(out, blob.Bytes);
    }

    for (const AssetRef& resource : package.Resources)
    {
        AppendPod(out, static_cast<std::uint16_t>(resource.Type));
        AppendString(out, resource.Path);
    }

    for (const UiUnsupportedFeature& note : package.Unsupported)
    {
        AppendString(out, note.Feature);
        AppendString(out, note.SourcePath);
        AppendPod(out, note.Line);
    }

    return true;
}

bool LoadSuiFromBytes(std::span<const std::byte> bytes, UiPackage& out, std::string* error)
{
    if (!LooksLikeSui(bytes.data(), bytes.size()))
    {
        SetError(error, "not a .sui container");
        return false;
    }

    Cursor cursor(bytes);
    SuiFileHeader header{};
    if (!cursor.Pod(header))
    {
        SetError(error, "truncated .sui header");
        return false;
    }
    if (header.Version != kSuiVersion)
    {
        SetError(error, "unsupported .sui version");
        return false;
    }
    if (header.HeaderSize < sizeof(SuiFileHeader) || header.HeaderSize > bytes.size())
    {
        SetError(error, "implausible .sui header size");
        return false;
    }
    // Honour the recorded header size rather than sizeof: a later version that
    // only grows the header stays readable by seeking past what it added.
    cursor.SeekTo(header.HeaderSize);

    UiPackage parsed;
    if (!cursor.String(parsed.RootDocumentName))
    {
        SetError(error, "truncated .sui root document name");
        return false;
    }

    parsed.Blobs.resize(header.BlobCount);
    for (UiPackageBlob& blob : parsed.Blobs)
    {
        std::uint16_t kind = 0;
        if (!cursor.String(blob.VirtualName) || !cursor.String(blob.SourcePath)
            || !cursor.Pod(kind) || !cursor.Blob(blob.Bytes))
        {
            SetError(error, "truncated .sui blob record");
            return false;
        }
        if (kind > static_cast<std::uint16_t>(UiBlobKind::StyleSheet))
        {
            SetError(error, "unknown .sui blob kind");
            return false;
        }
        blob.Kind = static_cast<UiBlobKind>(kind);
    }

    parsed.Resources.resize(header.ResourceCount);
    for (AssetRef& resource : parsed.Resources)
    {
        std::uint16_t type = 0;
        if (!cursor.Pod(type) || !cursor.String(resource.Path))
        {
            SetError(error, "truncated .sui resource record");
            return false;
        }
        if (type == 0 || type >= static_cast<std::uint16_t>(AssetType::Count))
        {
            SetError(error, "unknown .sui resource asset type");
            return false;
        }
        resource.Type = static_cast<AssetType>(type);
    }

    parsed.Unsupported.resize(header.UnsupportedCount);
    for (UiUnsupportedFeature& note : parsed.Unsupported)
    {
        if (!cursor.String(note.Feature) || !cursor.String(note.SourcePath)
            || !cursor.Pod(note.Line))
        {
            SetError(error, "truncated .sui capability note");
            return false;
        }
    }

    if (parsed.FindBlob(parsed.RootDocumentName) == nullptr)
    {
        SetError(error, ".sui root document names no blob in the package");
        return false;
    }

    out = std::move(parsed);
    return true;
}
