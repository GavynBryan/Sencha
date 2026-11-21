#include <save/SavedGame.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/FourCC.h>
#include <core/serialization/HashedChunkFile.h>
#include <core/serialization/Serialize.h>
#include <zone/WorldPartitionIds.h>

#include <utility>

namespace
{
    constexpr std::uint32_t kSavedGameMagic = MakeFourCC('S', 'S', 'A', 'V');
    constexpr std::uint32_t kSummaryChunk = MakeFourCC('S', 'U', 'M', 'M');
    constexpr std::uint32_t kZonesChunk = MakeFourCC('Z', 'O', 'N', 'E');
    constexpr std::uint32_t kSectionsChunk = MakeFourCC('S', 'E', 'C', 'T');
    constexpr std::uint32_t kHashChunk = MakeFourCC('H', 'A', 'S', 'H');
    constexpr std::uint32_t kChunkVersion = 1;
    constexpr std::uint32_t kMaxRecords = 1u << 24;
    constexpr std::uint32_t kMaxTextBytes = 1u << 26;

    const char* ChunkName(std::uint32_t id)
    {
        return id == kSummaryChunk ? "SUMM" : id == kZonesChunk ? "ZONE" : "SECT";
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

    bool WriteComponent(BinaryWriter& writer, const DurableComponentState& component)
    {
        return writer.Write(component.Entity.Value) && writer.Write(component.Type.Value)
            && writer.Write(component.Version) && SerializeTrivialArray(writer, component.Bytes);
    }

    bool ReadComponent(BinaryReader& reader, DurableComponentState& component)
    {
        return reader.Read(component.Entity.Value) && reader.Read(component.Type.Value)
            && reader.Read(component.Version) && DeserializeTrivialArray(reader, component.Bytes, kMaxTextBytes);
    }

    bool WriteZone(BinaryWriter& writer, const DurableZoneState& zone)
    {
        return writer.Write(zone.Zone.Value) && SerializeTrivialArray(writer, zone.Destroyed)
            && SerializeArray(writer, zone.Components, WriteComponent);
    }

    bool ReadZone(BinaryReader& reader, DurableZoneState& zone)
    {
        return reader.Read(zone.Zone.Value) && DeserializeTrivialArray(reader, zone.Destroyed, kMaxRecords)
            && DeserializeArray(reader, zone.Components, ReadComponent, kMaxRecords);
    }
}

std::vector<std::byte> EncodeSavedGame(const SavedGame& saved)
{
    HashedChunkFileWriter file(kSavedGameMagic, kSavedGameFormatVersion);
    BinaryWriter& writer = file.Writer();
    file.Chunk(kSummaryChunk, kChunkVersion, [&] { return WriteJson(writer, SummaryJson(saved.Summary)); });
    file.Chunk(kZonesChunk, kChunkVersion, [&] { return SerializeArray(writer, saved.Zones, WriteZone); });
    file.Chunk(kSectionsChunk, kChunkVersion, [&] { return WriteJson(writer, SectionsJson(saved.Sections)); });
    return file.Finish(kHashChunk);
}

DecodedSavedGame DecodeSavedGame(std::span<const std::byte> bytes)
{
    DecodedSavedGame decoded;
    SavedGame saved;
    bool summarized = false;
    std::optional<std::uint32_t> newerChunk;
    const auto read = [&](const ChunkHeader& chunk, BinaryReader& reader) {
        const bool known = chunk.Id == kSummaryChunk || chunk.Id == kZonesChunk || chunk.Id == kSectionsChunk;
        if (!known)
            return true;
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
        case kZonesChunk:
            return DeserializeArray(reader, saved.Zones, ReadZone, kMaxRecords);
        default:
            return ReadJson(reader, json) && ReadSections(json, saved.Sections);
        }
    };
    const HashedChunkFileFormat format{ .Magic = kSavedGameMagic,
                                        .OldestVersion = 1,
                                        .Version = kSavedGameFormatVersion,
                                        .HashChunk = kHashChunk,
                                        .Subject = "saved game",
                                        .VersionAdvice = "was written by a newer build" };
    switch (ReadHashedChunkFile(bytes, format, read, &decoded.Error))
    {
    case HashedChunkFileRead::Read:
        break;
    case HashedChunkFileRead::Newer:
        decoded.Newer = true;
        return decoded;
    case HashedChunkFileRead::Refused:
        return decoded;
    }
    if (newerChunk.has_value())
    {
        decoded.Newer = true;
        decoded.Error = std::string("the saved game's '") + ChunkName(*newerChunk) + "' chunk was written by a newer build";
        return decoded;
    }
    if (!summarized)
    {
        decoded.Error = "saved game has no summary";
        return decoded;
    }
    decoded.Saved = std::move(saved);
    return decoded;
}
