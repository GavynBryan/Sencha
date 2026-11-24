#include <zone/ZoneStateLists.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <core/serialization/Serialize.h>

#include <cstring>
#include <sstream>
#include <utility>

namespace
{
    constexpr std::uint32_t kMaxEntries = 1u << 24;
    constexpr std::uint32_t kMaxText = 1u << 26;

    std::vector<std::byte> BytesOf(const std::ostringstream& stream)
    {
        const std::string text = stream.str();
        std::vector<std::byte> bytes(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        return bytes;
    }

    class SpanStream
    {
    public:
        explicit SpanStream(std::span<const std::byte> bytes)
            : Stream(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()), std::ios::binary)
            , Reader(Stream)
        {
        }

        BinaryReader& Read() { return Reader; }
        [[nodiscard]] bool AtEnd() { return Stream.peek() == std::istream::traits_type::eof(); }

    private:
        std::istringstream Stream;
        BinaryReader Reader;
    };

    bool WriteBytes(BinaryWriter& writer, std::span<const std::byte> bytes)
    {
        return writer.Write(static_cast<std::uint32_t>(bytes.size()))
            && (bytes.empty()
                || writer.WriteBytes(reinterpret_cast<const char*>(bytes.data()),
                                     static_cast<std::streamsize>(bytes.size())));
    }

    bool ReadBytes(BinaryReader& reader, std::vector<std::byte>& bytes)
    {
        std::uint32_t size = 0;
        if (!reader.Read(size) || size > kMaxText)
            return false;
        bytes.resize(size);
        return size == 0 || reader.ReadBytes(reinterpret_cast<char*>(bytes.data()), size);
    }
}

bool IsKnownZoneList(std::string_view name)
{
    return name == kDestroyedZoneList || name == kChangedZoneList || name == kHeldZoneList || name == kGainedZoneList
        || name == kOccupiedZoneList;
}

EncodedZoneLists EncodeZoneLists(std::span<const ZoneStateList> lists)
{
    std::ostringstream stream(std::ios::binary);
    BinaryWriter writer(stream);
    bool ok = writer.Write(static_cast<std::uint32_t>(lists.size()));
    for (const ZoneStateList& list : lists)
        ok = ok && Serialize(writer, list.Name) && writer.Write(list.Version) && WriteBytes(writer, list.Payload);
    return ok ? std::make_shared<const std::vector<std::byte>>(BytesOf(stream)) : nullptr;
}

std::optional<std::vector<ZoneStateList>> DecodeZoneLists(std::span<const std::byte> bytes)
{
    SpanStream stream(bytes);
    std::uint32_t count = 0;
    if (!stream.Read().Read(count) || count > kMaxEntries)
        return std::nullopt;
    std::vector<ZoneStateList> lists(count);
    for (ZoneStateList& list : lists)
        if (!Deserialize(stream.Read(), list.Name, kMaxText) || !stream.Read().Read(list.Version)
            || !ReadBytes(stream.Read(), list.Payload))
            return std::nullopt;
    if (!stream.AtEnd())
        return std::nullopt;
    return lists;
}

std::vector<std::byte> EncodeOccupiedList(std::span<const SensorSeed> pairs)
{
    std::ostringstream stream(std::ios::binary);
    BinaryWriter writer(stream);
    bool ok = writer.Write(static_cast<std::uint32_t>(pairs.size()));
    for (const SensorSeed& pair : pairs)
        ok = ok && writer.Write(pair.Sensor.Value) && writer.Write(pair.Occupant.Value);
    return ok ? BytesOf(stream) : std::vector<std::byte>{};
}

std::optional<std::vector<SensorSeed>> DecodeOccupiedList(std::span<const std::byte> payload)
{
    SpanStream stream(payload);
    std::uint32_t count = 0;
    if (!stream.Read().Read(count) || count > kMaxEntries)
        return std::nullopt;
    std::vector<SensorSeed> pairs(count);
    for (SensorSeed& pair : pairs)
        if (!stream.Read().Read(pair.Sensor.Value) || !stream.Read().Read(pair.Occupant.Value))
            return std::nullopt;
    if (!stream.AtEnd())
        return std::nullopt;
    return pairs;
}

std::vector<std::byte> EncodeDestroyedList(std::span<const PersistentEntityId> destroyed)
{
    std::ostringstream stream(std::ios::binary);
    BinaryWriter writer(stream);
    bool ok = writer.Write(static_cast<std::uint32_t>(destroyed.size()));
    for (const PersistentEntityId id : destroyed)
        ok = ok && writer.Write(id.Value);
    return ok ? BytesOf(stream) : std::vector<std::byte>{};
}

std::optional<std::vector<PersistentEntityId>> DecodeDestroyedList(std::span<const std::byte> payload)
{
    SpanStream stream(payload);
    std::uint32_t count = 0;
    if (!stream.Read().Read(count) || count > kMaxEntries)
        return std::nullopt;
    std::vector<PersistentEntityId> destroyed(count);
    for (PersistentEntityId& id : destroyed)
        if (!stream.Read().Read(id.Value))
            return std::nullopt;
    if (!stream.AtEnd())
        return std::nullopt;
    return destroyed;
}

std::vector<std::byte> EncodeEntityList(std::span<const ZoneListEntity> entities)
{
    std::ostringstream stream(std::ios::binary);
    BinaryWriter writer(stream);
    bool ok = writer.Write(static_cast<std::uint32_t>(entities.size()));
    for (const ZoneListEntity& entity : entities)
        ok = ok && writer.Write(entity.Id.Value) && Serialize(writer, JsonStringify(entity.Value));
    return ok ? BytesOf(stream) : std::vector<std::byte>{};
}

std::optional<std::vector<ZoneListEntity>> DecodeEntityList(std::span<const std::byte> payload)
{
    SpanStream stream(payload);
    std::uint32_t count = 0;
    if (!stream.Read().Read(count) || count > kMaxEntries)
        return std::nullopt;
    std::vector<ZoneListEntity> entities(count);
    std::string text;
    for (ZoneListEntity& entity : entities)
    {
        if (!stream.Read().Read(entity.Id.Value) || !Deserialize(stream.Read(), text, kMaxText))
            return std::nullopt;
        std::optional<JsonValue> value = JsonParse(text);
        if (!value.has_value() || !value->IsObject())
            return std::nullopt;
        entity.Value = std::move(*value);
    }
    if (!stream.AtEnd())
        return std::nullopt;
    return entities;
}

std::vector<std::byte> EncodeGainedList(std::span<const GainedZoneMember> members)
{
    std::ostringstream stream(std::ios::binary);
    BinaryWriter writer(stream);
    bool ok = writer.Write(static_cast<std::uint32_t>(members.size()));
    for (const GainedZoneMember& member : members)
    {
        std::ostringstream inner(std::ios::binary);
        BinaryWriter body(inner);
        ok = ok && Serialize(body, member.Form) && WriteBytes(body, member.Payload)
            && WriteBytes(writer, BytesOf(inner));
    }
    return ok ? BytesOf(stream) : std::vector<std::byte>{};
}

std::optional<std::vector<GainedZoneMember>> DecodeGainedList(std::span<const std::byte> payload)
{
    SpanStream stream(payload);
    std::uint32_t count = 0;
    if (!stream.Read().Read(count) || count > kMaxEntries)
        return std::nullopt;
    std::vector<GainedZoneMember> members(count);
    std::vector<std::byte> framed;
    for (GainedZoneMember& member : members)
    {
        if (!ReadBytes(stream.Read(), framed))
            return std::nullopt;
        SpanStream body(framed);
        if (!Deserialize(body.Read(), member.Form, kMaxText) || !ReadBytes(body.Read(), member.Payload)
            || !body.AtEnd())
            return std::nullopt;
    }
    if (!stream.AtEnd())
        return std::nullopt;
    return members;
}

std::vector<std::byte> EncodeWholeGainedMember(const ZoneListEntity& entity)
{
    std::ostringstream stream(std::ios::binary);
    BinaryWriter writer(stream);
    const bool ok = writer.Write(entity.Id.Value) && Serialize(writer, JsonStringify(entity.Value));
    return ok ? BytesOf(stream) : std::vector<std::byte>{};
}

std::optional<ZoneListEntity> DecodeWholeGainedMember(std::span<const std::byte> payload)
{
    SpanStream stream(payload);
    ZoneListEntity entity;
    std::string text;
    if (!stream.Read().Read(entity.Id.Value) || !Deserialize(stream.Read(), text, kMaxText) || !stream.AtEnd())
        return std::nullopt;
    std::optional<JsonValue> value = JsonParse(text);
    if (!value.has_value() || !value->IsObject())
        return std::nullopt;
    entity.Value = std::move(*value);
    return entity;
}

std::optional<PersistentEntityId> WholeGainedMemberId(std::span<const std::byte> payload)
{
    PersistentEntityId id;
    if (payload.size() < sizeof(id.Value))
        return std::nullopt;
    SpanStream stream(payload.first(sizeof(id.Value)));
    if (!stream.Read().Read(id.Value))
        return std::nullopt;
    return id;
}
