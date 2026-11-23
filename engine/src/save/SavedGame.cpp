#include <save/SavedGame.h>

#include <core/hash/ContentHash.h>
#include <core/json/JsonEquals.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/FourCC.h>
#include <core/serialization/HashedChunkFile.h>
#include <core/serialization/Serialize.h>
#include <zone/WorldPartitionIds.h>

#include <cstring>
#include <sstream>
#include <utility>

namespace
{
    constexpr std::uint32_t kSavedGameMagic = MakeFourCC('S', 'S', 'A', 'V');
    constexpr std::uint32_t kSummaryChunk = MakeFourCC('S', 'U', 'M', 'M');
    constexpr std::uint32_t kThumbnailChunk = MakeFourCC('T', 'H', 'M', 'B');
    constexpr std::uint32_t kHeadChunk = MakeFourCC('H', 'E', 'A', 'D');
    constexpr std::uint32_t kLevelsChunk = MakeFourCC('L', 'V', 'L', 'S');
    constexpr std::uint32_t kSectionsChunk = MakeFourCC('S', 'E', 'C', 'T');
    constexpr std::uint32_t kHashChunk = MakeFourCC('H', 'A', 'S', 'H');
    constexpr std::uint32_t kChunkVersion = 1;
    constexpr std::uint32_t kOldestFormatVersion = 2;
    constexpr std::uint32_t kMaxRecords = 1u << 24;
    constexpr std::uint32_t kMaxTextBytes = 1u << 26;
    constexpr std::size_t kHeaderBytes = 8;
    constexpr std::size_t kChunkHeaderBytes = 12;

    int RankOf(std::uint32_t id)
    {
        switch (id)
        {
        case kSummaryChunk:
            return 0;
        case kThumbnailChunk:
            return 1;
        case kHeadChunk:
            return 2;
        case kLevelsChunk:
            return 3;
        case kSectionsChunk:
            return 4;
        default:
            return -1;
        }
    }

    std::string ChunkName(std::uint32_t id)
    {
        std::string name(4, ' ');
        for (int i = 0; i < 4; ++i)
            name[static_cast<std::size_t>(i)] = static_cast<char>((id >> (8 * i)) & 0xff);
        return name;
    }

    const char* KindName(LevelKind kind) { return kind == LevelKind::World ? "world" : "scene"; }

    JsonValue SummaryJson(const SavedGameSummary& summary)
    {
        JsonValue::Object level{ { "kind", JsonValue(std::string(KindName(summary.Level.Kind))) },
                                 { "name", JsonValue(summary.Level.Name) } };
        JsonValue::Object root{ { "level", JsonValue(std::move(level)) },
                                { "saved_at", JsonValue(static_cast<double>(summary.SavedAtUnixSeconds)) } };
        if (summary.Focus.has_value())
            root.emplace_back("focus", JsonValue(ZoneIdToString(*summary.Focus)));
        if (!summary.Game.IsNull())
            root.emplace_back("game", summary.Game);
        return JsonValue(std::move(root));
    }

    bool ReadSummary(const JsonValue& root, SavedGameSummary& out)
    {
        const JsonValue* level = root.Find("level");
        const JsonValue* kind = level != nullptr ? level->Find("kind") : nullptr;
        const JsonValue* name = level != nullptr ? level->Find("name") : nullptr;
        if (kind == nullptr || !kind->IsString() || name == nullptr || !name->IsString())
            return false;
        if (kind->AsString() != "scene" && kind->AsString() != "world")
            return false;
        out.Level = LevelReference{ .Kind = kind->AsString() == "world" ? LevelKind::World : LevelKind::Scene,
                                    .Name = name->AsString() };
        out.SavedAtUnixSeconds = static_cast<std::uint64_t>(root.NumberOr("saved_at", 0.0));
        if (const JsonValue* focus = root.Find("focus"))
        {
            out.Focus = focus->IsString() ? ZoneIdFromString(focus->AsString()) : std::nullopt;
            if (!out.Focus.has_value())
                return false;
        }
        if (const JsonValue* game = root.Find("game"))
            out.Game = *game;
        return true;
    }

    JsonValue SectionsJson(std::span<const SavedGameSection> sections)
    {
        JsonValue::Array array;
        for (const SavedGameSection& section : sections)
            array.emplace_back(JsonValue::Object{ { "name", JsonValue(section.Name) },
                                                  { "version", JsonValue(static_cast<double>(section.Version)) },
                                                  { "data", section.Data } });
        return JsonValue(std::move(array));
    }

    bool ReadSections(const JsonValue& root, std::vector<SavedGameSection>& out)
    {
        if (!root.IsArray())
            return false;
        for (const JsonValue& entry : root.AsArray())
        {
            const JsonValue* name = entry.Find("name");
            const JsonValue* data = entry.Find("data");
            if (name == nullptr || !name->IsString() || data == nullptr)
                return false;
            out.push_back(SavedGameSection{ .Name = name->AsString(),
                                            .Version = static_cast<std::uint32_t>(entry.NumberOr("version", 1.0)),
                                            .Data = *data });
        }
        return true;
    }

    bool WriteJson(BinaryWriter& writer, const JsonValue& value)
    {
        return Serialize(writer, JsonStringify(value));
    }

    bool ReadJson(BinaryReader& reader, JsonValue& out)
    {
        std::string text;
        if (!Deserialize(reader, text, kMaxTextBytes))
            return false;
        std::optional<JsonValue> parsed = JsonParse(text);
        if (!parsed.has_value())
            return false;
        out = std::move(*parsed);
        return true;
    }

    bool WriteRaw(BinaryWriter& writer, std::span<const std::byte> bytes)
    {
        return bytes.empty()
            || writer.WriteBytes(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    bool ReadRaw(BinaryReader& reader, std::size_t size, std::vector<std::byte>& out)
    {
        out.resize(size);
        return size == 0 || reader.ReadBytes(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    }

    std::vector<std::byte> BytesOf(const std::ostringstream& stream)
    {
        const std::string text = stream.str();
        std::vector<std::byte> bytes(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        return bytes;
    }

    std::uint32_t ListCount(const EncodedZoneLists& lists)
    {
        std::uint32_t count = 0;
        if (lists != nullptr && lists->size() >= sizeof(count))
            std::memcpy(&count, lists->data(), sizeof(count));
        return count;
    }

    std::span<const std::byte> ListBytes(const EncodedZoneLists& lists)
    {
        return lists != nullptr && lists->size() >= sizeof(std::uint32_t)
            ? std::span<const std::byte>(*lists).subspan(sizeof(std::uint32_t))
            : std::span<const std::byte>{};
    }

    std::vector<std::byte> ZoneRecord(const DurableZoneState& zone)
    {
        std::ostringstream stream(std::ios::binary);
        BinaryWriter writer(stream);
        const bool ok = writer.Write(zone.Zone.Value) && writer.Write(ListCount(zone.Lists) + ListCount(zone.Held))
            && WriteRaw(writer, ListBytes(zone.Lists)) && WriteRaw(writer, ListBytes(zone.Held));
        return ok ? BytesOf(stream) : std::vector<std::byte>{};
    }

    std::vector<std::byte> LevelRecord(const DurableLevelState& level)
    {
        std::ostringstream stream(std::ios::binary);
        BinaryWriter writer(stream);
        bool ok = writer.Write(static_cast<std::uint8_t>(level.Level.Kind)) && Serialize(writer, level.Level.Name)
            && writer.Write(static_cast<std::uint32_t>(level.Zones.size()));
        for (const DurableZoneState& zone : level.Zones)
        {
            const std::vector<std::byte> record = ZoneRecord(zone);
            ok = ok && !record.empty() && writer.Write(static_cast<std::uint32_t>(record.size()))
                && WriteRaw(writer, record);
        }
        return ok ? BytesOf(stream) : std::vector<std::byte>{};
    }

    bool WriteLevels(BinaryWriter& writer, std::span<const DurableLevelState> levels)
    {
        bool ok = writer.Write(static_cast<std::uint32_t>(levels.size()));
        for (const DurableLevelState& level : levels)
        {
            const std::vector<std::byte> record = LevelRecord(level);
            ok = ok && !record.empty() && writer.Write(static_cast<std::uint32_t>(record.size()))
                && WriteRaw(writer, record);
        }
        return ok;
    }

    struct ListCheck
    {
        bool Malformed = false;
        std::string Newer;
    };

    ListCheck CheckList(const ZoneStateList& list)
    {
        if (!IsKnownZoneList(list.Name))
            return {};
        if (list.Version > kZoneListVersion)
            return { .Malformed = false, .Newer = list.Name };
        bool read = true;
        if (list.Name == kDestroyedZoneList)
            read = DecodeDestroyedList(list.Payload).has_value();
        else if (list.Name == kChangedZoneList || list.Name == kHeldZoneList)
            read = DecodeEntityList(list.Payload).has_value();
        else if (list.Name == kGainedZoneList)
            read = DecodeGainedList(list.Payload).has_value();
        return { .Malformed = !read, .Newer = {} };
    }

    struct LevelsRead
    {
        std::vector<DurableLevelState> Levels;
        std::string Newer;
        bool Malformed = false;
    };

    std::optional<DurableZoneState> ReadZone(std::span<const std::byte> record, LevelsRead& read)
    {
        if (record.size() < sizeof(std::uint64_t))
            return std::nullopt;
        DurableZoneState zone;
        std::memcpy(&zone.Zone.Value, record.data(), sizeof(std::uint64_t));
        std::optional<std::vector<ZoneStateList>> lists = DecodeZoneLists(record.subspan(sizeof(std::uint64_t)));
        if (!lists.has_value())
            return std::nullopt;
        std::vector<ZoneStateList> kept;
        std::vector<ZoneStateList> held;
        for (ZoneStateList& list : *lists)
        {
            const ListCheck check = CheckList(list);
            if (check.Malformed)
                read.Malformed = true;
            if (!check.Newer.empty() && read.Newer.empty())
                read.Newer = check.Newer;
            (list.Name == kHeldZoneList && list.Version <= kZoneListVersion ? held : kept).push_back(std::move(list));
        }
        if (!kept.empty())
            zone.Lists = EncodeZoneLists(kept);
        if (!held.empty())
            zone.Held = EncodeZoneLists(held);
        return zone;
    }

    bool ReadLevels(BinaryReader& reader, LevelsRead& read)
    {
        std::uint32_t count = 0;
        if (!reader.Read(count) || count > kMaxRecords)
            return false;
        std::vector<std::byte> record;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t size = 0;
            if (!reader.Read(size) || size > kMaxTextBytes || !ReadRaw(reader, size, record))
                return false;
            const std::string body(reinterpret_cast<const char*>(record.data()), record.size());
            std::istringstream stream(body, std::ios::binary);
            BinaryReader level(stream);
            std::uint8_t kind = 0;
            DurableLevelState state;
            std::uint32_t zones = 0;
            if (!level.Read(kind) || kind > static_cast<std::uint8_t>(LevelKind::World)
                || !Deserialize(level, state.Level.Name, kMaxTextBytes) || !level.Read(zones) || zones > kMaxRecords)
                return false;
            state.Level.Kind = static_cast<LevelKind>(kind);
            std::vector<std::byte> zoneRecord;
            for (std::uint32_t z = 0; z < zones; ++z)
            {
                std::uint32_t zoneSize = 0;
                if (!level.Read(zoneSize) || zoneSize > kMaxTextBytes || !ReadRaw(level, zoneSize, zoneRecord))
                    return false;
                std::optional<DurableZoneState> zone = ReadZone(zoneRecord, read);
                if (!zone.has_value())
                    return false;
                state.Zones.push_back(std::move(*zone));
            }
            if (stream.peek() != std::istream::traits_type::eof())
                return false;
            read.Levels.push_back(std::move(state));
        }
        return true;
    }

    template <typename T>
    bool ReadAt(std::span<const std::byte> bytes, std::size_t offset, T& out)
    {
        if (offset + sizeof(T) > bytes.size())
            return false;
        std::memcpy(&out, bytes.data() + offset, sizeof(T));
        return true;
    }

    struct HeadPlace
    {
        std::string Error;
        bool Newer = false;
        std::size_t Offset = 0;
        std::size_t End = 0;
        std::size_t Wanted = 0;
    };

    HeadPlace NoHead(std::string error, std::size_t wanted = 0, bool newer = false)
    {
        return { .Error = std::move(error), .Newer = newer, .Offset = 0, .End = 0, .Wanted = wanted };
    }

    HeadPlace FindHead(std::span<const std::byte> bytes)
    {
        std::uint32_t magic = 0;
        std::uint32_t version = 0;
        if (!ReadAt(bytes, 0, magic) || !ReadAt(bytes, 4, version))
            return NoHead("not a saved game", kHeaderBytes);
        if (magic != kSavedGameMagic)
            return NoHead("not a saved game");
        if (version < kOldestFormatVersion || version > kSavedGameFormatVersion)
            return NoHead("saved game version " + std::to_string(version)
                              + (version > kSavedGameFormatVersion ? " was written by a newer build"
                                                                   : " is older than this build reads"),
                          0, version > kSavedGameFormatVersion);
        for (std::size_t offset = kHeaderBytes;;)
        {
            std::uint32_t id = 0;
            std::uint32_t size = 0;
            if (!ReadAt(bytes, offset, id) || !ReadAt(bytes, offset + 8, size))
                return NoHead("the saved game has no head", offset + kChunkHeaderBytes);
            if (id == kHeadChunk)
                return { .Error = {}, .Newer = false, .Offset = offset, .End = offset + kChunkHeaderBytes + size,
                         .Wanted = 0 };
            if (id == kHashChunk || (offset == kHeaderBytes && id != kSummaryChunk))
                return NoHead("the saved game's summary is not first or it has no head");
            offset += kChunkHeaderBytes + size;
        }
    }

    HeadPlace CheckPrefix(std::span<const std::byte> bytes)
    {
        HeadPlace head = FindHead(bytes);
        if (!head.Error.empty())
            return head;
        std::uint64_t stored = 0;
        if (!ReadAt(bytes, head.Offset + kChunkHeaderBytes, stored) || stored != HashBytes64(bytes.first(head.Offset)))
            return NoHead("saved game head mismatch");
        return head;
    }

    std::optional<SavedGameThumbnail> ReadThumbnail(std::span<const std::byte> payload)
    {
        const std::string body(reinterpret_cast<const char*>(payload.data()), payload.size());
        std::istringstream stream(body, std::ios::binary);
        BinaryReader reader(stream);
        SavedGameThumbnail thumbnail;
        if (!reader.Read(thumbnail.Width) || !reader.Read(thumbnail.Height)
            || !Deserialize(reader, thumbnail.Encoding, kMaxTextBytes)
            || !DeserializeTrivialArray(reader, thumbnail.Bytes, static_cast<std::uint32_t>(payload.size()))
            || stream.peek() != std::istream::traits_type::eof())
            return std::nullopt;
        return thumbnail;
    }

    const HashedChunkFileFormat kFormat{ .Magic = kSavedGameMagic,
                                         .OldestVersion = kOldestFormatVersion,
                                         .Version = kSavedGameFormatVersion,
                                         .HashChunk = kHashChunk,
                                         .Subject = "saved game",
                                         .VersionAdvice = "is not one this build reads" };
}

bool operator==(const SavedGameSummary& a, const SavedGameSummary& b)
{
    return a.Level == b.Level && a.Focus == b.Focus && a.SavedAtUnixSeconds == b.SavedAtUnixSeconds
        && JsonEquals(a.Game, b.Game);
}

std::vector<std::byte> EncodeSavedGame(const SavedGame& saved)
{
    HashedChunkFileWriter file(kSavedGameMagic, kSavedGameFormatVersion);
    BinaryWriter& writer = file.Writer();
    const auto carry = [&](bool prefix) {
        for (const SavedGameChunk& chunk : saved.Carried)
            if (chunk.InPrefix == prefix)
                file.Chunk(chunk.Id, chunk.Version, [&] { return WriteRaw(writer, chunk.Payload); });
    };
    file.Chunk(kSummaryChunk, kChunkVersion, [&] { return WriteJson(writer, SummaryJson(saved.Summary)); });
    if (saved.Thumbnail.has_value())
        file.Chunk(kThumbnailChunk, saved.Thumbnail->Version, [&] { return WriteRaw(writer, saved.Thumbnail->Payload); });
    carry(true);
    file.HashChunk(kHeadChunk);
    file.Chunk(kLevelsChunk, kChunkVersion, [&] { return WriteLevels(writer, saved.Levels); });
    file.Chunk(kSectionsChunk, kChunkVersion, [&] { return WriteJson(writer, SectionsJson(saved.Sections)); });
    carry(false);
    return file.Finish(kHashChunk);
}

DecodedSavedGameSummary DecodeSavedGameSummary(std::span<const std::byte> prefix)
{
    DecodedSavedGameSummary decoded;
    const HeadPlace checked = CheckPrefix(prefix);
    if (!checked.Error.empty())
    {
        decoded.Error = checked.Error;
        decoded.Newer = checked.Newer;
        return decoded;
    }
    for (std::size_t offset = kHeaderBytes; offset < checked.Offset;)
    {
        std::uint32_t id = 0;
        std::uint32_t version = 0;
        std::uint32_t size = 0;
        (void)ReadAt(prefix, offset, id);
        (void)ReadAt(prefix, offset + 4, version);
        (void)ReadAt(prefix, offset + 8, size);
        if (id == kSummaryChunk)
        {
            if (version > kChunkVersion)
            {
                decoded.Newer = true;
                decoded.Error = "the saved game's 'SUMM' chunk was written by a newer build";
                return decoded;
            }
            const std::string body(reinterpret_cast<const char*>(prefix.data()) + offset + kChunkHeaderBytes, size);
            std::istringstream stream(body, std::ios::binary);
            BinaryReader reader(stream);
            JsonValue json;
            SavedGameSummary summary;
            if (!ReadJson(reader, json) || !ReadSummary(json, summary))
            {
                decoded.Error = "malformed saved game summary";
                return decoded;
            }
            decoded.Summary = std::move(summary);
        }
        if (id == kThumbnailChunk)
        {
            const auto* payload = prefix.data() + offset + kChunkHeaderBytes;
            decoded.Thumbnail = SavedGameChunk{
                .Id = id, .Version = version, .Payload = std::vector<std::byte>(payload, payload + size), .InPrefix = true };
        }
        offset += kChunkHeaderBytes + size;
    }
    if (!decoded.Summary.has_value())
        decoded.Error = "saved game has no summary";
    return decoded;
}

std::size_t SavedGamePrefixWanted(std::span<const std::byte> read)
{
    const HeadPlace head = FindHead(read);
    if (head.Wanted > 0)
        return head.Wanted;
    return head.Error.empty() ? head.End : read.size();
}

SavedGameChunk EncodeSavedGameThumbnail(const SavedGameThumbnail& thumbnail)
{
    std::ostringstream stream(std::ios::binary);
    BinaryWriter writer(stream);
    const bool written = writer.Write(thumbnail.Width) && writer.Write(thumbnail.Height)
        && Serialize(writer, thumbnail.Encoding) && SerializeTrivialArray(writer, thumbnail.Bytes);
    return SavedGameChunk{ .Id = kThumbnailChunk,
                           .Version = kChunkVersion,
                           .Payload = written ? BytesOf(stream) : std::vector<std::byte>{},
                           .InPrefix = true };
}

std::optional<SavedGameThumbnail> DecodeSavedGameThumbnail(const SavedGameChunk& chunk)
{
    if (chunk.Id != kThumbnailChunk || chunk.Version != kChunkVersion)
        return std::nullopt;
    return ReadThumbnail(chunk.Payload);
}

DecodedSavedGame DecodeSavedGame(std::span<const std::byte> bytes)
{
    DecodedSavedGame decoded;
    if (const HeadPlace checked = CheckPrefix(bytes); !checked.Error.empty())
    {
        decoded.Error = checked.Error;
        decoded.Newer = checked.Newer;
        return decoded;
    }
    SavedGame saved;
    bool summarized = false;
    bool headed = false;
    int lastRank = -1;
    std::optional<std::uint32_t> newerChunk;
    LevelsRead levels;
    std::string disorder;
    const auto read = [&](const ChunkHeader& chunk, BinaryReader& reader) {
        const int rank = RankOf(chunk.Id);
        if (rank < 0)
        {
            SavedGameChunk carried{ .Id = chunk.Id, .Version = chunk.Version, .Payload = {}, .InPrefix = !headed };
            if (!ReadRaw(reader, chunk.Size, carried.Payload))
                return false;
            saved.Carried.push_back(std::move(carried));
            return true;
        }
        if (rank <= lastRank)
        {
            disorder = ChunkName(chunk.Id);
            return false;
        }
        lastRank = rank;
        if (chunk.Id == kHeadChunk)
        {
            headed = true;
            return true;
        }
        if (chunk.Id == kThumbnailChunk)
        {
            saved.Thumbnail = SavedGameChunk{ .Id = chunk.Id, .Version = chunk.Version, .Payload = {}, .InPrefix = true };
            return ReadRaw(reader, chunk.Size, saved.Thumbnail->Payload);
        }
        if (chunk.Version != kChunkVersion)
        {
            if (chunk.Version > kChunkVersion && !newerChunk.has_value())
                newerChunk = chunk.Id;
            return chunk.Version > kChunkVersion;
        }
        JsonValue json;
        switch (chunk.Id)
        {
        case kSummaryChunk:
            summarized = ReadJson(reader, json) && ReadSummary(json, saved.Summary);
            return summarized;
        case kLevelsChunk:
            return ReadLevels(reader, levels);
        default:
            return ReadJson(reader, json) && ReadSections(json, saved.Sections);
        }
    };
    switch (ReadHashedChunkFile(bytes, kFormat, read, &decoded.Error))
    {
    case HashedChunkFileRead::Read:
        break;
    case HashedChunkFileRead::Newer:
        decoded.Newer = true;
        return decoded;
    case HashedChunkFileRead::Refused:
        if (!disorder.empty())
            decoded.Error = "the saved game's '" + disorder + "' chunk is out of the stated order";
        return decoded;
    }
    if (newerChunk.has_value())
    {
        decoded.Newer = true;
        decoded.Error = "the saved game's '" + ChunkName(*newerChunk) + "' chunk was written by a newer build";
        return decoded;
    }
    if (!levels.Newer.empty())
    {
        decoded.Newer = true;
        decoded.Error = "the saved game's '" + levels.Newer + "' list was written by a newer build";
        return decoded;
    }
    if (levels.Malformed)
    {
        decoded.Error = "the saved game holds a list this build cannot read";
        return decoded;
    }
    if (!summarized)
    {
        decoded.Error = "saved game has no summary";
        return decoded;
    }
    saved.Levels = std::move(levels.Levels);
    decoded.Saved = std::move(saved);
    return decoded;
}
