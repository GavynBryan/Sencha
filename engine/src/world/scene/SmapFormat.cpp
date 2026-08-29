#include <world/scene/SmapFormat.h>

#include <core/assets/AssetManifest.h>
#include <world/build/EntityBuildPackage.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <bit>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace
{
//-----------------------------------------------------------------------------
// Wire primitives. Everything multi-byte is little-endian, written and read
// through shifts so the format is identical on any host. Unsigned varints are
// LEB128. Numbers are IEEE f64 bit images.
//-----------------------------------------------------------------------------

void SetError(SmapError* error, std::string message)
{
    if (error != nullptr)
        error->Message = std::move(message);
}

std::string HexId(std::uint64_t value)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i)
    {
        out[static_cast<std::size_t>(i)] = digits[value & 0xF];
        value >>= 4;
    }
    return out;
}

class ByteBuilder
{
public:
    void U8(std::uint8_t value) { Bytes_.push_back(static_cast<std::byte>(value)); }

    void U32(std::uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            U8(static_cast<std::uint8_t>(value >> (8 * i)));
    }

    void U64(std::uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
            U8(static_cast<std::uint8_t>(value >> (8 * i)));
    }

    void F32(float value) { U32(std::bit_cast<std::uint32_t>(value)); }
    void F64(double value) { U64(std::bit_cast<std::uint64_t>(value)); }

    void Varint(std::uint64_t value)
    {
        while (value >= 0x80)
        {
            U8(static_cast<std::uint8_t>(value) | 0x80);
            value >>= 7;
        }
        U8(static_cast<std::uint8_t>(value));
    }

    void Raw(std::string_view text)
    {
        const auto* data = reinterpret_cast<const std::byte*>(text.data());
        Bytes_.insert(Bytes_.end(), data, data + text.size());
    }

    void Append(const ByteBuilder& other)
    {
        Bytes_.insert(Bytes_.end(), other.Bytes_.begin(), other.Bytes_.end());
    }

    // Overwrites bytes written earlier, for the header's content hash: the
    // hash covers the section payloads, which do not exist yet when the
    // header is laid down.
    void PatchU64(std::size_t offset, std::uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
            Bytes_[offset + static_cast<std::size_t>(i)] =
                static_cast<std::byte>(static_cast<std::uint8_t>(value >> (8 * i)));
    }

    [[nodiscard]] std::size_t Size() const { return Bytes_.size(); }
    [[nodiscard]] std::span<const std::byte> View() const { return Bytes_; }
    [[nodiscard]] std::vector<std::byte> Take() { return std::move(Bytes_); }

private:
    std::vector<std::byte> Bytes_;
};

// Bounds-checked cursor over an untrusted byte range. Every read either
// succeeds completely or reports failure; a failed reader never advances past
// the end. Failure carries no message of its own -- the caller names what it
// was reading.
class ByteReader
{
public:
    explicit ByteReader(std::span<const std::byte> bytes) : Bytes_(bytes) {}

    [[nodiscard]] bool AtEnd() const { return Offset_ == Bytes_.size(); }
    [[nodiscard]] std::size_t Remaining() const { return Bytes_.size() - Offset_; }

    // Guards a reserve() against a hostile or corrupt element count: true when
    // `count` items of at least `minimumSize` bytes each could still follow.
    [[nodiscard]] bool CanHold(std::uint64_t count, std::size_t minimumSize) const
    {
        return count <= Remaining() / minimumSize;
    }

    [[nodiscard]] bool U8(std::uint8_t& out)
    {
        if (Offset_ >= Bytes_.size())
            return false;
        out = static_cast<std::uint8_t>(Bytes_[Offset_++]);
        return true;
    }

    [[nodiscard]] bool U32(std::uint32_t& out)
    {
        std::uint64_t wide = 0;
        if (!Little(4, wide))
            return false;
        out = static_cast<std::uint32_t>(wide);
        return true;
    }

    [[nodiscard]] bool U64(std::uint64_t& out) { return Little(8, out); }

    [[nodiscard]] bool F32(float& out)
    {
        std::uint32_t bits = 0;
        if (!U32(bits))
            return false;
        out = std::bit_cast<float>(bits);
        return true;
    }

    [[nodiscard]] bool F64(double& out)
    {
        std::uint64_t bits = 0;
        if (!U64(bits))
            return false;
        out = std::bit_cast<double>(bits);
        return true;
    }

    [[nodiscard]] bool Varint(std::uint64_t& out)
    {
        out = 0;
        for (int shift = 0; shift < 64; shift += 7)
        {
            std::uint8_t byte = 0;
            if (!U8(byte))
                return false;
            out |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0)
                return true;
        }
        return false; // more than 10 continuation bytes: malformed
    }

    [[nodiscard]] bool Text(std::size_t length, std::string& out)
    {
        if (length > Bytes_.size() - Offset_)
            return false;
        out.assign(reinterpret_cast<const char*>(Bytes_.data() + Offset_), length);
        Offset_ += length;
        return true;
    }

private:
    [[nodiscard]] bool Little(std::size_t count, std::uint64_t& out)
    {
        if (count > Bytes_.size() - Offset_)
            return false;
        out = 0;
        for (std::size_t i = 0; i < count; ++i)
            out |= static_cast<std::uint64_t>(
                       static_cast<std::uint8_t>(Bytes_[Offset_ + i]))
                   << (8 * i);
        Offset_ += count;
        return true;
    }

    std::span<const std::byte> Bytes_;
    std::size_t Offset_ = 0;
};

//-----------------------------------------------------------------------------
// Hashing. FNV-1a, the same construction ComponentTypeId uses; the content
// hash runs over the section payloads in directory order so any corruption or
// truncation is caught before a single record is interpreted.
//-----------------------------------------------------------------------------

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

class Fnv1a
{
public:
    void Byte(std::uint8_t value)
    {
        Hash_ ^= value;
        Hash_ *= kFnvPrime;
    }

    void Bytes(std::span<const std::byte> bytes)
    {
        for (const std::byte b : bytes)
            Byte(static_cast<std::uint8_t>(b));
    }

    void Text(std::string_view text)
    {
        for (const char c : text)
            Byte(static_cast<std::uint8_t>(c));
        Byte(0); // terminator so adjacent strings cannot alias
    }

    void U64(std::uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
            Byte(static_cast<std::uint8_t>(value >> (8 * i)));
    }

    [[nodiscard]] std::uint64_t Value() const { return Hash_; }

private:
    std::uint64_t Hash_ = kFnvOffset;
};

//-----------------------------------------------------------------------------
// String interning. First-encounter order over the writer's fixed traversal
// (dependency paths, then entity component trees, then collision paths), which
// is what makes write-twice byte-identical.
//-----------------------------------------------------------------------------

class StringTableBuilder
{
public:
    std::uint64_t Intern(const std::string& text)
    {
        const auto [it, added] =
            Indices_.try_emplace(text, static_cast<std::uint64_t>(Strings_.size()));
        if (added)
            Strings_.push_back(text);
        return it->second;
    }

    [[nodiscard]] const std::vector<std::string>& Strings() const { return Strings_; }

private:
    std::unordered_map<std::string, std::uint64_t> Indices_;
    std::vector<std::string> Strings_;
};

//-----------------------------------------------------------------------------
// Value codec. A tagged tree mirroring the JSON value set; object keys and
// string values are table indices. Self-delimiting, so component records need
// no length prefix -- the content hash has already vouched for the bytes.
//-----------------------------------------------------------------------------

enum class ValueTag : std::uint8_t
{
    Null = 0,
    False = 1,
    True = 2,
    Number = 3,
    String = 4,
    Array = 5,
    Object = 6,
};

void EncodeValue(const JsonValue& value, StringTableBuilder& strings, ByteBuilder& out)
{
    if (value.IsNull())
    {
        out.U8(static_cast<std::uint8_t>(ValueTag::Null));
    }
    else if (value.IsBool())
    {
        out.U8(static_cast<std::uint8_t>(value.AsBool() ? ValueTag::True
                                                        : ValueTag::False));
    }
    else if (value.IsNumber())
    {
        out.U8(static_cast<std::uint8_t>(ValueTag::Number));
        out.F64(value.AsNumber());
    }
    else if (value.IsString())
    {
        out.U8(static_cast<std::uint8_t>(ValueTag::String));
        out.Varint(strings.Intern(value.AsString()));
    }
    else if (value.IsArray())
    {
        out.U8(static_cast<std::uint8_t>(ValueTag::Array));
        out.Varint(value.AsArray().size());
        for (const JsonValue& element : value.AsArray())
            EncodeValue(element, strings, out);
    }
    else
    {
        out.U8(static_cast<std::uint8_t>(ValueTag::Object));
        out.Varint(value.AsObject().size());
        for (const auto& [key, member] : value.AsObject())
        {
            out.Varint(strings.Intern(key));
            EncodeValue(member, strings, out);
        }
    }
}

// Depth-capped like the JSON parsers, so a crafted file cannot recurse the
// reader off the stack.
constexpr int kMaxValueDepth = 256;

[[nodiscard]] bool DecodeValue(ByteReader& reader,
                               const std::vector<std::string>& strings,
                               JsonValue& out,
                               int depth)
{
    if (depth > kMaxValueDepth)
        return false;

    std::uint8_t tag = 0;
    if (!reader.U8(tag))
        return false;

    switch (static_cast<ValueTag>(tag))
    {
    case ValueTag::Null:
        out = JsonValue();
        return true;
    case ValueTag::False:
        out = JsonValue(false);
        return true;
    case ValueTag::True:
        out = JsonValue(true);
        return true;
    case ValueTag::Number:
    {
        double number = 0.0;
        if (!reader.F64(number))
            return false;
        out = JsonValue(number);
        return true;
    }
    case ValueTag::String:
    {
        std::uint64_t index = 0;
        if (!reader.Varint(index) || index >= strings.size())
            return false;
        out = JsonValue(strings[static_cast<std::size_t>(index)]);
        return true;
    }
    case ValueTag::Array:
    {
        std::uint64_t count = 0;
        if (!reader.Varint(count) || !reader.CanHold(count, 1))
            return false;
        JsonValue::Array array;
        array.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i)
        {
            JsonValue element;
            if (!DecodeValue(reader, strings, element, depth + 1))
                return false;
            array.push_back(std::move(element));
        }
        out = JsonValue(std::move(array));
        return true;
    }
    case ValueTag::Object:
    {
        std::uint64_t count = 0;
        // at least one key byte + one value byte per member
        if (!reader.Varint(count) || !reader.CanHold(count, 2))
            return false;
        JsonValue::Object object;
        object.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i)
        {
            std::uint64_t keyIndex = 0;
            if (!reader.Varint(keyIndex) || keyIndex >= strings.size())
                return false;
            JsonValue member;
            if (!DecodeValue(reader, strings, member, depth + 1))
                return false;
            object.emplace_back(strings[static_cast<std::size_t>(keyIndex)],
                                std::move(member));
        }
        out = JsonValue(std::move(object));
        return true;
    }
    }
    return false; // unknown tag
}

// The persistent id an entity's own persistent_id component carries, when the
// component is present and well-formed. Used to keep the record's identity
// field and the component from ever disagreeing on disk.
[[nodiscard]] std::optional<PersistentEntityId>
ComponentPersistentId(const IComponentSerializer& serializer, const JsonValue& payload)
{
    if (serializer.JsonKey() != "persistent_id" || !payload.IsObject())
        return std::nullopt;
    const JsonValue* id = payload.Find("id");
    if (id == nullptr || !id->IsString())
        return std::nullopt;
    return PersistentEntityIdFromString(id->AsString());
}

struct SectionRange
{
    std::uint32_t Id = 0;
    std::uint64_t Offset = 0;
    std::uint64_t Size = 0;
};

// The container below the records: header validated, content hash verified,
// string table decoded, one bounds-checked reader per remaining section.
struct ParsedContainer
{
    std::uint64_t ContentHash = 0;
    std::vector<std::string> Strings;
    ByteReader Dependencies{ {} };
    ByteReader Entities{ {} };
    ByteReader Collision{ {} };
};

[[nodiscard]] bool ParseContainer(std::span<const std::byte> bytes,
                                  ParsedContainer& out,
                                  SmapError* error)
{
    ByteReader header(bytes);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t sectionCount = 0;
    if (!header.U32(magic) || magic != kSmapMagic)
    {
        SetError(error, "Not a .smap file: bad magic.");
        return false;
    }
    if (!header.U32(version) || version != kSmapVersion)
    {
        SetError(error, "Unsupported .smap version " + std::to_string(version)
                            + " (this build reads version "
                            + std::to_string(kSmapVersion)
                            + "); recook the level.");
        return false;
    }
    if (!header.U64(out.ContentHash) || !header.U32(sectionCount)
        || sectionCount > 64)
    {
        SetError(error, "Truncated or malformed .smap header.");
        return false;
    }

    std::vector<SectionRange> directory;
    directory.reserve(sectionCount);
    for (std::uint32_t i = 0; i < sectionCount; ++i)
    {
        SectionRange range;
        if (!header.U32(range.Id) || !header.U64(range.Offset)
            || !header.U64(range.Size) || range.Offset > bytes.size()
            || range.Size > bytes.size() - range.Offset)
        {
            SetError(error, "Truncated or malformed .smap section directory.");
            return false;
        }
        directory.push_back(range);
    }

    // Verify content before interpreting a single record. This is the whole
    // corruption story: past this point malformed data means a writer bug,
    // not a damaged file.
    Fnv1a computed;
    for (const SectionRange& range : directory)
        computed.Bytes(bytes.subspan(static_cast<std::size_t>(range.Offset),
                                     static_cast<std::size_t>(range.Size)));
    if (computed.Value() != out.ContentHash)
    {
        SetError(error, ".smap content hash mismatch: the file is corrupt or "
                        "was truncated after cooking.");
        return false;
    }

    const auto section = [&](std::uint32_t id) -> std::optional<ByteReader> {
        for (const SectionRange& range : directory)
            if (range.Id == id)
                return ByteReader(
                    bytes.subspan(static_cast<std::size_t>(range.Offset),
                                  static_cast<std::size_t>(range.Size)));
        return std::nullopt;
    };

    auto tableReader = section(kSmapSectionStrings);
    auto depsReader = section(kSmapSectionDependencies);
    auto entityReader = section(kSmapSectionEntities);
    auto collisionReader = section(kSmapSectionCollision);
    if (!tableReader || !depsReader || !entityReader || !collisionReader)
    {
        SetError(error, ".smap is missing a required section.");
        return false;
    }
    out.Dependencies = *depsReader;
    out.Entities = *entityReader;
    out.Collision = *collisionReader;

    std::uint64_t count = 0;
    if (!tableReader->Varint(count) || !tableReader->CanHold(count, 1))
    {
        SetError(error, "Malformed .smap string table.");
        return false;
    }
    out.Strings.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i)
    {
        std::uint64_t length = 0;
        std::string text;
        if (!tableReader->Varint(length)
            || !tableReader->Text(static_cast<std::size_t>(length), text))
        {
            SetError(error, "Malformed .smap string table.");
            return false;
        }
        out.Strings.push_back(std::move(text));
    }
    if (!tableReader->AtEnd())
    {
        SetError(error, ".smap string table has trailing bytes.");
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseDependencies(ParsedContainer& container,
                                     SmapContents& contents,
                                     SmapError* error)
{
    ByteReader& reader = container.Dependencies;
    std::uint64_t count = 0;
    // id + at least one path-index byte
    if (!reader.Varint(count) || !reader.CanHold(count, 9))
    {
        SetError(error, "Malformed .smap dependency table.");
        return false;
    }
    contents.Dependencies.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i)
    {
        std::uint64_t id = 0;
        std::uint64_t pathIndex = 0;
        if (!reader.U64(id) || !reader.Varint(pathIndex)
            || pathIndex >= container.Strings.size())
        {
            SetError(error, "Malformed .smap dependency table.");
            return false;
        }
        contents.Dependencies.push_back(SmapDependency{
            AssetId{ id },
            container.Strings[static_cast<std::size_t>(pathIndex)] });
    }
    if (!reader.AtEnd())
    {
        SetError(error, ".smap dependency table has trailing bytes.");
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseCollision(ParsedContainer& container,
                                  SmapContents& contents,
                                  SmapError* error)
{
    ByteReader& reader = container.Collision;
    std::uint64_t count = 0;
    // at least one path-index byte + the three origin floats
    if (!reader.Varint(count) || !reader.CanHold(count, 13))
    {
        SetError(error, "Malformed .smap collision section.");
        return false;
    }
    contents.Collision.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i)
    {
        std::uint64_t pathIndex = 0;
        SmapCollisionCell cell;
        if (!reader.Varint(pathIndex) || pathIndex >= container.Strings.size()
            || !reader.F32(cell.Origin.X) || !reader.F32(cell.Origin.Y)
            || !reader.F32(cell.Origin.Z))
        {
            SetError(error, "Malformed .smap collision cell.");
            return false;
        }
        cell.BlobPath = container.Strings[static_cast<std::size_t>(pathIndex)];
        contents.Collision.push_back(std::move(cell));
    }
    if (!reader.AtEnd())
    {
        SetError(error, ".smap collision section has trailing bytes.");
        return false;
    }
    return true;
}

[[nodiscard]] bool ReadFileBytes(const std::filesystem::path& path,
                                 std::vector<std::byte>& out,
                                 SmapError* error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        SetError(error, "Could not open '" + path.generic_string() + "'.");
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size < 0)
    {
        SetError(error, "Could not read '" + path.generic_string() + "'.");
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(out.data()),
              static_cast<std::streamsize>(out.size()));
    if (!file.good() && !out.empty())
    {
        SetError(error, "Could not read '" + path.generic_string() + "'.");
        return false;
    }
    return true;
}

} // namespace

std::uint64_t ComponentSchemaFingerprint(const IComponentSerializer& serializer)
{
    Fnv1a hash;
    hash.Text(serializer.JsonKey());
    for (const RuntimeField& field : serializer.RuntimeFields())
    {
        hash.Text(field.Name);
        hash.Byte(static_cast<std::uint8_t>(field.Scalar));
        hash.U64(field.Offset);
        hash.U64(field.Size);
        hash.Byte(field.Count);
        hash.Byte(static_cast<std::uint8_t>(static_cast<std::uint16_t>(field.Asset)));
        hash.Byte(static_cast<std::uint8_t>(static_cast<std::uint16_t>(field.Asset) >> 8));
        hash.Byte(static_cast<std::uint8_t>(field.Arity));
    }
    return hash.Value();
}

bool WriteSmap(const SmapContents& contents,
               const ComponentSerializerRegistry& serializers,
               std::vector<std::byte>& out,
               SmapError* error)
{
    // Validate structure before emitting a byte: parent ordinals in range and
    // acyclic, every component known to this build, identity coherent. Ranges
    // first, so the cycle walks below never step through an unchecked ordinal.
    const std::size_t entityCount = contents.Entities.size();
    for (std::size_t i = 0; i < entityCount; ++i)
    {
        const std::uint32_t parent = contents.Entities[i].Parent;
        if (parent != UINT32_MAX && parent >= entityCount)
        {
            SetError(error, "Scene entity " + std::to_string(i)
                                + " parents to ordinal " + std::to_string(parent)
                                + ", past the last entity.");
            return false;
        }
    }
    for (std::size_t i = 0; i < entityCount; ++i)
    {
        // Walk the parent chain; a walk longer than the entity count has
        // revisited someone.
        std::uint32_t cursor = contents.Entities[i].Parent;
        for (std::size_t steps = 0; cursor != UINT32_MAX; ++steps)
        {
            if (cursor == i || steps > entityCount)
            {
                SetError(error, "Scene entity " + std::to_string(i)
                                    + " is part of a parent cycle.");
                return false;
            }
            cursor = contents.Entities[cursor].Parent;
        }
    }
    for (std::size_t i = 0; i < entityCount; ++i)
    {
        const SmapEntityRecord& record = contents.Entities[i];
        std::optional<PersistentEntityId> componentId;
        for (const auto& [type, payload] : record.Components)
        {
            const IComponentSerializer* serializer = serializers.FindByType(type);
            if (serializer == nullptr)
            {
                SetError(error, "Scene entity " + std::to_string(i)
                                    + " carries component id "
                                    + HexId(type.Value)
                                    + " with no registered serializer; it "
                                      "cannot be cooked by this build.");
                return false;
            }
            if (const auto id = ComponentPersistentId(*serializer, payload))
                componentId = id;
        }

        const PersistentEntityId recorded = record.Persistent;
        const PersistentEntityId carried = componentId.value_or(PersistentEntityId{});
        if (recorded != carried)
        {
            SetError(error, "Scene entity " + std::to_string(i)
                                + "'s recorded persistent id "
                                + HexId(recorded.Value)
                                + " disagrees with its persistent_id component ("
                                + HexId(carried.Value) + ").");
            return false;
        }
    }

    // Encode the value-bearing sections against a shared table. The table is
    // populated by this traversal, so STRS is serialized last but stored
    // first.
    StringTableBuilder strings;

    ByteBuilder deps;
    deps.Varint(contents.Dependencies.size());
    for (const SmapDependency& dependency : contents.Dependencies)
    {
        deps.U64(dependency.Id.Value);
        deps.Varint(strings.Intern(dependency.Path));
    }

    ByteBuilder entities;
    entities.Varint(entityCount);
    for (const SmapEntityRecord& record : contents.Entities)
    {
        entities.U64(record.Persistent.Value);
        entities.U32(record.Parent);
        entities.Varint(record.Components.size());
        for (const auto& [type, payload] : record.Components)
        {
            entities.U64(type.Value);
            entities.U64(ComponentSchemaFingerprint(*serializers.FindByType(type)));
            EncodeValue(payload, strings, entities);
        }
    }

    ByteBuilder collision;
    collision.Varint(contents.Collision.size());
    for (const SmapCollisionCell& cell : contents.Collision)
    {
        collision.Varint(strings.Intern(cell.BlobPath));
        collision.F32(cell.Origin.X);
        collision.F32(cell.Origin.Y);
        collision.F32(cell.Origin.Z);
    }

    ByteBuilder table;
    table.Varint(strings.Strings().size());
    for (const std::string& text : strings.Strings())
    {
        table.Varint(text.size());
        table.Raw(text);
    }

    // Assemble: header, directory, then the four sections in fixed order. The
    // content hash covers the section payloads in directory order.
    const std::pair<std::uint32_t, const ByteBuilder*> sections[] = {
        { kSmapSectionStrings, &table },
        { kSmapSectionDependencies, &deps },
        { kSmapSectionEntities, &entities },
        { kSmapSectionCollision, &collision },
    };

    ByteBuilder file;
    file.U32(kSmapMagic);
    file.U32(kSmapVersion);
    const std::size_t hashOffset = file.Size();
    file.U64(0); // content hash, patched below
    file.U32(static_cast<std::uint32_t>(std::size(sections)));

    std::uint64_t offset =
        file.Size() + std::size(sections) * (4 + 8 + 8); // past the directory
    Fnv1a contentHash;
    for (const auto& [id, payload] : sections)
    {
        file.U32(id);
        file.U64(offset);
        file.U64(payload->Size());
        offset += payload->Size();
        contentHash.Bytes(payload->View());
    }
    for (const auto& [id, payload] : sections)
        file.Append(*payload);

    file.PatchU64(hashOffset, contentHash.Value());
    out = file.Take();
    if (error != nullptr)
        error->Message.clear();
    return true;
}

bool ReadSmap(std::span<const std::byte> bytes,
              const ComponentSerializerRegistry& serializers,
              SmapContents& out,
              SmapError* error)
{
    ParsedContainer container;
    if (!ParseContainer(bytes, container, error))
        return false;

    SmapContents contents;
    contents.ContentHash = container.ContentHash;
    if (!ParseDependencies(container, contents, error))
        return false;

    ByteReader& entityReader = container.Entities;
    std::uint64_t entityCount = 0;
    // persistent id + parent + at least one component-count byte
    if (!entityReader.Varint(entityCount) || !entityReader.CanHold(entityCount, 13))
    {
        SetError(error, "Malformed .smap entity section.");
        return false;
    }
    contents.Entities.reserve(static_cast<std::size_t>(entityCount));
    for (std::uint64_t i = 0; i < entityCount; ++i)
    {
        SmapEntityRecord record;
        std::uint64_t persistent = 0;
        std::uint64_t componentCount = 0;
        if (!entityReader.U64(persistent) || !entityReader.U32(record.Parent)
            || !entityReader.Varint(componentCount)
            // type id + fingerprint + at least one payload byte
            || !entityReader.CanHold(componentCount, 17))
        {
            SetError(error, "Malformed .smap entity record.");
            return false;
        }
        record.Persistent = PersistentEntityId{ persistent };
        if (record.Parent != UINT32_MAX && record.Parent >= entityCount)
        {
            SetError(error, ".smap entity " + std::to_string(i)
                                + " parents to an unknown entity.");
            return false;
        }

        record.Components.reserve(static_cast<std::size_t>(componentCount));
        for (std::uint64_t c = 0; c < componentCount; ++c)
        {
            std::uint64_t typeValue = 0;
            std::uint64_t fingerprint = 0;
            if (!entityReader.U64(typeValue) || !entityReader.U64(fingerprint))
            {
                SetError(error, "Malformed .smap component record.");
                return false;
            }

            const ComponentTypeId type{ typeValue };
            const IComponentSerializer* serializer = serializers.FindByType(type);
            if (serializer == nullptr)
            {
                SetError(error, ".smap carries component id " + HexId(typeValue)
                                    + ", which this build does not register; "
                                      "the cook and this runtime disagree on "
                                      "the component set.");
                return false;
            }
            const std::uint64_t expected = ComponentSchemaFingerprint(*serializer);
            if (fingerprint != expected)
            {
                SetError(error, "Component '" + std::string(serializer->JsonKey())
                                    + "' was cooked with schema fingerprint "
                                    + HexId(fingerprint)
                                    + " but this build expects "
                                    + HexId(expected) + "; recook the level.");
                return false;
            }

            JsonValue payload;
            if (!DecodeValue(entityReader, container.Strings, payload, 0))
            {
                SetError(error, "Malformed payload for component '"
                                    + std::string(serializer->JsonKey())
                                    + "' in .smap.");
                return false;
            }
            record.Components.emplace_back(type, std::move(payload));
        }
        contents.Entities.push_back(std::move(record));
    }
    if (!entityReader.AtEnd())
    {
        SetError(error, ".smap entity section has trailing bytes.");
        return false;
    }

    if (!ParseCollision(container, contents, error))
        return false;

    out = std::move(contents);
    if (error != nullptr)
        error->Message.clear();
    return true;
}

bool ReadSmapMetadata(std::span<const std::byte> bytes,
                      SmapContents& out,
                      SmapError* error)
{
    ParsedContainer container;
    if (!ParseContainer(bytes, container, error))
        return false;

    SmapContents contents;
    contents.ContentHash = container.ContentHash;
    if (!ParseDependencies(container, contents, error)
        || !ParseCollision(container, contents, error))
        return false;

    out = std::move(contents);
    if (error != nullptr)
        error->Message.clear();
    return true;
}

bool ReadSmapFile(const std::filesystem::path& path,
                  const ComponentSerializerRegistry& serializers,
                  SmapContents& out,
                  SmapError* error)
{
    std::vector<std::byte> bytes;
    if (!ReadFileBytes(path, bytes, error))
        return false;
    if (!ReadSmap(bytes, serializers, out, error))
    {
        if (error != nullptr)
            error->Message = "'" + path.generic_string() + "': " + error->Message;
        return false;
    }
    return true;
}

bool ReadSmapMetadataFile(const std::filesystem::path& path,
                          SmapContents& out,
                          SmapError* error)
{
    std::vector<std::byte> bytes;
    if (!ReadFileBytes(path, bytes, error))
        return false;
    if (!ReadSmapMetadata(bytes, out, error))
    {
        if (error != nullptr)
            error->Message = "'" + path.generic_string() + "': " + error->Message;
        return false;
    }
    return true;
}

bool BuildEntityPackageFromSmap(const SmapContents& contents,
                                const ComponentSerializerRegistry& serializers,
                                EntityBuildPackage& package,
                                SmapError* error)
{
    return BuildEntityPackageFromSmap(contents, serializers, package,
                                      SmapPackageOptions{}, error);
}

bool BuildEntityPackageFromSmap(const SmapContents& contents,
                                const ComponentSerializerRegistry& serializers,
                                EntityBuildPackage& package,
                                const SmapPackageOptions& options,
                                SmapError* error)
{
    EntityBuildPackage built;
    std::vector<PackageEntityId> entities;
    entities.reserve(contents.Entities.size());

    for (std::size_t i = 0; i < contents.Entities.size(); ++i)
    {
        const SmapEntityRecord& record = contents.Entities[i];
        const PackageEntityId entity = built.CreateEntity();
        entities.push_back(entity);

        for (const auto& [type, payload] : record.Components)
        {
            const IComponentSerializer* serializer = serializers.FindByType(type);
            if (serializer == nullptr)
            {
                SetError(error, ".smap entity " + std::to_string(i)
                                    + " carries unregistered component id "
                                    + HexId(type.Value) + ".");
                return false;
            }
            if (options.StripPersistentIdentity
                && serializer->JsonKey() == "persistent_id")
                continue;
            if (!built.AddSerializedJson(entity, type, payload))
            {
                SetError(error, ".smap entity " + std::to_string(i)
                                    + " carries component '"
                                    + std::string(serializer->JsonKey())
                                    + "' twice.");
                return false;
            }
        }

        if (!options.StripPersistentIdentity && record.Persistent.IsValid())
            (void)built.SetPersistentId(entity, record.Persistent);
    }

    for (std::size_t i = 0; i < contents.Entities.size(); ++i)
    {
        const std::uint32_t parent = contents.Entities[i].Parent;
        if (parent == UINT32_MAX)
            continue;
        if (parent >= entities.size()
            || !built.SetParent(entities[i], entities[parent]))
        {
            SetError(error, ".smap entity " + std::to_string(i)
                                + " has an invalid parent relation.");
            return false;
        }
    }

    package = std::move(built);
    if (error != nullptr)
        error->Message.clear();
    return true;
}

std::vector<std::string> ResolveSmapDependencyPaths(
    std::span<const SmapDependency> dependencies, const AssetRegistry& registry)
{
    AssetManifest manifest;
    manifest.Entries.reserve(dependencies.size());
    for (const SmapDependency& dependency : dependencies)
        manifest.Entries.push_back(
            AssetManifestEntry{ dependency.Id, dependency.Path });
    return ResolveManifestPaths(manifest, registry);
}
