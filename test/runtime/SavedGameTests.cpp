#include <core/hash/ContentHash.h>
#include <core/io/FileBytes.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/FourCC.h>
#include <core/serialization/HashedChunkFile.h>
#include <core/serialization/Serialize.h>
#include <save/SaveSlotFiles.h>
#include <save/SavedGame.h>

#include <gtest/gtest.h>

#include <cstring>
#include <sstream>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    constexpr std::uint32_t kMagic = MakeFourCC('S', 'S', 'A', 'V');
    constexpr std::uint32_t kSumm = MakeFourCC('S', 'U', 'M', 'M');
    constexpr std::uint32_t kHead = MakeFourCC('H', 'E', 'A', 'D');
    constexpr std::uint32_t kLvls = MakeFourCC('L', 'V', 'L', 'S');
    constexpr std::uint32_t kSect = MakeFourCC('S', 'E', 'C', 'T');
    constexpr std::uint32_t kThmb = MakeFourCC('T', 'H', 'M', 'B');
    constexpr std::uint32_t kHash = MakeFourCC('H', 'A', 'S', 'H');
    constexpr ZoneId kCourtyard{ 0x1234abcd5678ef00 };

    std::vector<std::byte> Bytes(std::initializer_list<int> values)
    {
        std::vector<std::byte> bytes;
        for (const int value : values)
            bytes.push_back(static_cast<std::byte>(value));
        return bytes;
    }

    EncodedZoneLists Lists(std::vector<ZoneStateList> lists) { return EncodeZoneLists(lists); }

    ZoneStateList Changed(std::uint32_t version = kZoneListVersion)
    {
        const ZoneListEntity door{ PersistentEntityId{ 0x30 },
                                   *JsonParse(R"({"GameplayTags":{"tags":{"door.unsealed":{"name":"door.unsealed","stacks":1}}}})") };
        return { std::string(kChangedZoneList), version, EncodeEntityList(std::span(&door, 1)) };
    }

    SavedGame Example()
    {
        SavedGame saved;
        saved.Summary.Level = LevelReference{ .Kind = LevelKind::World, .Name = "garden" };
        saved.Summary.Focus = kCourtyard;
        saved.Summary.SavedAtUnixSeconds = 1'763'000'000;
        saved.Summary.Game = *JsonParse(R"({"area":"Courtyard","play_seconds":5400})");

        const std::vector destroyed{ PersistentEntityId{ 0x10 }, PersistentEntityId{ 0x20 } };
        const GainedZoneMember gained{ "from_source", Bytes({ 1, 2, 3 }) };
        const ZoneListEntity record{ PersistentEntityId{ PersistentEntityIdRuntimeBit | 1 },
                                     *JsonParse(R"({"Held":{"holder":"0000000000000030","position":0}})") };
        DurableZoneState courtyard{
            .Zone = kCourtyard,
            .Lists = Lists({ { std::string(kDestroyedZoneList), kZoneListVersion, EncodeDestroyedList(destroyed) },
                             Changed(),
                             { std::string(kGainedZoneList), kZoneListVersion, EncodeGainedList(std::span(&gained, 1)) },
                             { "occupied", 1, Bytes({ 9, 9 }) } }),
            .Held = Lists({ { std::string(kHeldZoneList), kZoneListVersion, EncodeEntityList(std::span(&record, 1)) } }),
        };
        saved.Levels.push_back(DurableLevelState{ saved.Summary.Level, { courtyard } });

        const std::vector cellar{ PersistentEntityId{ 0x40 } };
        saved.Levels.push_back(DurableLevelState{
            LevelReference{ .Kind = LevelKind::Scene, .Name = "levels/cellar" },
            { DurableZoneState{ .Zone = kSceneLevelZone,
                                .Lists = Lists({ { std::string(kDestroyedZoneList), kZoneListVersion,
                                                   EncodeDestroyedList(cellar) } }),
                                .Held = nullptr } } });
        saved.Sections.push_back(SavedGameSection{
            .Name = "game.notes", .Version = 2, .Data = JsonValue(JsonValue::Object{ { "seen", JsonValue(true) } }) });
        saved.Carried.push_back(SavedGameChunk{ .Id = MakeFourCC('F', 'U', 'T', 'R'), .Version = 1,
                                                .Payload = Bytes({ 42, 0, 0, 0 }), .InPrefix = false });
        return saved;
    }

    bool SameLists(const EncodedZoneLists& a, const EncodedZoneLists& b)
    {
        return (a == nullptr && b == nullptr) || (a != nullptr && b != nullptr && *a == *b);
    }

    void ExpectSame(const SavedGame& actual, const SavedGame& expected)
    {
        EXPECT_EQ(actual.Summary, expected.Summary);
        EXPECT_EQ(actual.Thumbnail, expected.Thumbnail);
        EXPECT_EQ(actual.Carried, expected.Carried);
        ASSERT_EQ(actual.Levels.size(), expected.Levels.size());
        for (std::size_t i = 0; i < actual.Levels.size(); ++i)
        {
            EXPECT_EQ(actual.Levels[i].Level, expected.Levels[i].Level);
            ASSERT_EQ(actual.Levels[i].Zones.size(), expected.Levels[i].Zones.size());
            for (std::size_t z = 0; z < actual.Levels[i].Zones.size(); ++z)
            {
                EXPECT_EQ(actual.Levels[i].Zones[z].Zone, expected.Levels[i].Zones[z].Zone);
                EXPECT_TRUE(SameLists(actual.Levels[i].Zones[z].Lists, expected.Levels[i].Zones[z].Lists));
                EXPECT_TRUE(SameLists(actual.Levels[i].Zones[z].Held, expected.Levels[i].Zones[z].Held));
            }
        }
        ASSERT_EQ(actual.Sections.size(), expected.Sections.size());
        for (std::size_t i = 0; i < actual.Sections.size(); ++i)
        {
            EXPECT_EQ(actual.Sections[i].Name, expected.Sections[i].Name);
            EXPECT_EQ(actual.Sections[i].Version, expected.Sections[i].Version);
            EXPECT_EQ(JsonStringify(actual.Sections[i].Data), JsonStringify(expected.Sections[i].Data));
        }
    }

    struct ChunkAt
    {
        std::uint32_t Id = 0;
        std::size_t Offset = 0;
        std::uint32_t Size = 0;
    };

    std::vector<ChunkAt> ChunksOf(const std::vector<std::byte>& bytes)
    {
        std::vector<ChunkAt> chunks;
        for (std::size_t offset = 8; offset + 12 <= bytes.size();)
        {
            ChunkAt chunk;
            chunk.Offset = offset;
            std::memcpy(&chunk.Id, bytes.data() + offset, 4);
            std::memcpy(&chunk.Size, bytes.data() + offset + 8, 4);
            chunks.push_back(chunk);
            offset += 12 + chunk.Size;
        }
        return chunks;
    }

    std::size_t PrefixThroughHead(const std::vector<std::byte>& bytes)
    {
        for (const ChunkAt& chunk : ChunksOf(bytes))
            if (chunk.Id == kHead)
                return chunk.Offset + 12 + chunk.Size;
        return 0;
    }

    std::vector<std::byte> PayloadOf(const std::vector<std::byte>& bytes, std::uint32_t id)
    {
        for (const ChunkAt& chunk : ChunksOf(bytes))
            if (chunk.Id == id)
                return std::vector<std::byte>(bytes.begin() + static_cast<std::ptrdiff_t>(chunk.Offset + 12),
                                              bytes.begin() + static_cast<std::ptrdiff_t>(chunk.Offset + 12 + chunk.Size));
        return {};
    }

    template <typename T>
    T At(const std::vector<std::byte>& bytes, std::size_t offset)
    {
        T value{};
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return value;
    }

    std::vector<std::byte> FirstZoneRecord(const std::vector<std::byte>& levels)
    {
        const std::size_t level = 4 + 4;
        const auto nameLength = At<std::uint32_t>(levels, level + 1);
        const std::size_t zone = level + 1 + 4 + nameLength + 4;
        const auto size = At<std::uint32_t>(levels, zone);
        return std::vector<std::byte>(levels.begin() + static_cast<std::ptrdiff_t>(zone + 4),
                                      levels.begin() + static_cast<std::ptrdiff_t>(zone + 4 + size));
    }

    std::vector<std::byte> LevelsHolding(const std::vector<std::byte>& zoneRecord)
    {
        std::vector<std::byte> level;
        const auto put = [&](std::vector<std::byte>& out, const void* data, std::size_t size) {
            const auto* bytes = static_cast<const std::byte*>(data);
            out.insert(out.end(), bytes, bytes + size);
        };
        const std::uint8_t kind = 0;
        const std::string name = "levels/spliced";
        const auto nameLength = static_cast<std::uint32_t>(name.size());
        const std::uint32_t one = 1;
        const auto zoneSize = static_cast<std::uint32_t>(zoneRecord.size());
        put(level, &kind, 1);
        put(level, &nameLength, 4);
        put(level, name.data(), name.size());
        put(level, &one, 4);
        put(level, &zoneSize, 4);
        put(level, zoneRecord.data(), zoneRecord.size());
        std::vector<std::byte> levels;
        const auto levelSize = static_cast<std::uint32_t>(level.size());
        put(levels, &one, 4);
        put(levels, &levelSize, 4);
        put(levels, level.data(), level.size());
        return levels;
    }

    std::vector<std::byte> FileOf(std::initializer_list<std::pair<std::uint32_t, std::vector<std::byte>>> chunks,
                                  std::uint32_t version = kSavedGameFormatVersion)
    {
        HashedChunkFileWriter file(kMagic, version);
        for (const auto& [id, payload] : chunks)
        {
            if (id == kHead)
            {
                file.HashChunk(kHead);
                continue;
            }
            file.Chunk(id, 1, [&] {
                return payload.empty()
                    || file.Writer().WriteBytes(reinterpret_cast<const char*>(payload.data()),
                                                static_cast<std::streamsize>(payload.size()));
            });
        }
        return file.Finish(kHash);
    }

    std::vector<std::byte> JsonPayload(std::string_view text)
    {
        std::ostringstream stream(std::ios::binary);
        BinaryWriter writer(stream);
        EXPECT_TRUE(Serialize(writer, std::string(text)));
        const std::string written = stream.str();
        std::vector<std::byte> bytes(written.size());
        std::memcpy(bytes.data(), written.data(), written.size());
        return bytes;
    }

    const std::vector<std::byte> kRoomSummary = JsonPayload(R"({"level":{"kind":"scene","name":"room"},"saved_at":5})");
    const std::vector<std::byte> kNoLevels = Bytes({ 0, 0, 0, 0 });
    const std::vector<std::byte> kNoSections = JsonPayload("[]");

    const std::filesystem::path kFixture = SENCHA_REPO_ROOT "/test/fixtures/saves/example.v2.ssav";
}

TEST(SavedGame, WhatIsEncodedDecodesTheSame)
{
    const std::vector<std::byte> bytes = EncodeSavedGame(Example());
    ASSERT_FALSE(bytes.empty());
    const DecodedSavedGame decoded = DecodeSavedGame(bytes);
    ASSERT_TRUE(decoded.Saved.has_value()) << decoded.Error;
    ExpectSame(*decoded.Saved, Example());
}

TEST(SavedGame, TheSecondFormatsFixtureReadsAndIsWhatTheEncoderWrites)
{
    std::vector<std::byte> bytes;
    ASSERT_TRUE(ReadFileBytes(kFixture, bytes)) << kFixture;
    const DecodedSavedGame decoded = DecodeSavedGame(bytes);
    ASSERT_TRUE(decoded.Saved.has_value()) << decoded.Error;
    ExpectSame(*decoded.Saved, Example());
    ASSERT_EQ(kSavedGameFormatVersion, 2u) << "a new format keeps this fixture and checks in its own";
    EXPECT_EQ(EncodeSavedGame(Example()), bytes) << "format 2's writer drifted from its checked-in bytes";
    EXPECT_EQ(EncodeSavedGame(*decoded.Saved), bytes) << "what was read is written back byte for byte";
}

TEST(SavedGame, AFirstFormatFileIsRefusedByItsVersion)
{
    const DecodedSavedGame decoded = DecodeSavedGame(FileOf({ { kSumm, kRoomSummary } }, 1));
    EXPECT_FALSE(decoded.Saved.has_value());
    EXPECT_FALSE(decoded.Newer);
    EXPECT_NE(decoded.Error.find("version 1"), std::string::npos) << decoded.Error;
}

TEST(SavedGame, TheSummaryAndItsGameMemberAreReadAndCheckedFromThePrefixAlone)
{
    const std::vector<std::byte> bytes = EncodeSavedGame(Example());
    const std::size_t prefix = PrefixThroughHead(bytes);
    ASSERT_GT(prefix, 0u);
    ASSERT_LT(prefix, bytes.size());
    const std::vector<std::byte> head(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(prefix));
    const DecodedSavedGameSummary summary = DecodeSavedGameSummary(head);
    ASSERT_TRUE(summary.Summary.has_value()) << summary.Error;
    EXPECT_EQ(*summary.Summary, Example().Summary);
    EXPECT_EQ(JsonStringify(summary.Summary->Game), R"({"area":"Courtyard","play_seconds":5400})");
    EXPECT_FALSE(summary.HasThumbnail);
}

TEST(SavedGame, APrefixWithOneByteChangedFailsTheHead)
{
    std::vector<std::byte> bytes = EncodeSavedGame(Example());
    const std::size_t prefix = PrefixThroughHead(bytes);
    std::vector<std::byte> head(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(prefix));
    head[20] ^= std::byte{ 0x01 };
    const DecodedSavedGameSummary summary = DecodeSavedGameSummary(head);
    EXPECT_FALSE(summary.Summary.has_value());
    EXPECT_NE(summary.Error.find("head"), std::string::npos) << summary.Error;
}

TEST(SavedGame, AThumbnailIsReadBeforeTheHeadAndWrittenBackIntact)
{
    SavedGame saved = Example();
    saved.Thumbnail = SavedGameChunk{ .Id = kThmb, .Version = 1, .Payload = Bytes({ 2, 0, 1, 0, 3, 'p', 'n', 'g', 7 }),
                                      .InPrefix = true };
    const std::vector<std::byte> bytes = EncodeSavedGame(saved);
    const std::vector<ChunkAt> chunks = ChunksOf(bytes);
    ASSERT_GE(chunks.size(), 3u);
    EXPECT_EQ(chunks[0].Id, kSumm);
    EXPECT_EQ(chunks[1].Id, kThmb);
    EXPECT_EQ(chunks[2].Id, kHead);

    const DecodedSavedGame decoded = DecodeSavedGame(bytes);
    ASSERT_TRUE(decoded.Saved.has_value()) << decoded.Error;
    EXPECT_EQ(decoded.Saved->Thumbnail, saved.Thumbnail);
    EXPECT_EQ(EncodeSavedGame(*decoded.Saved), bytes);
    const std::vector<std::byte> head(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(PrefixThroughHead(bytes)));
    EXPECT_TRUE(DecodeSavedGameSummary(head).HasThumbnail);
}

TEST(SavedGame, ChunksOutOfTheStatedOrderAreRefused)
{
    const DecodedSavedGame sectionsFirst =
        DecodeSavedGame(FileOf({ { kSumm, kRoomSummary }, { kHead, {} }, { kSect, kNoSections }, { kLvls, kNoLevels } }));
    EXPECT_FALSE(sectionsFirst.Saved.has_value());
    EXPECT_NE(sectionsFirst.Error.find("LVLS"), std::string::npos) << sectionsFirst.Error;

    const DecodedSavedGame summaryLate = DecodeSavedGame(FileOf({ { kLvls, kNoLevels }, { kSumm, kRoomSummary }, { kHead, {} } }));
    EXPECT_FALSE(summaryLate.Saved.has_value());

    const DecodedSavedGame noHead = DecodeSavedGame(FileOf({ { kSumm, kRoomSummary }, { kLvls, kNoLevels } }));
    EXPECT_FALSE(noHead.Saved.has_value());

    const DecodedSavedGame stated = DecodeSavedGame(
        FileOf({ { kSumm, kRoomSummary }, { kHead, {} }, { kLvls, kNoLevels }, { kSect, kNoSections } }));
    EXPECT_TRUE(stated.Saved.has_value()) << stated.Error;
}

TEST(SavedGame, AZoneRecordCutFromOneFileAndSplicedIntoAnotherDecodes)
{
    const std::vector<std::byte> source = EncodeSavedGame(Example());
    const std::vector<std::byte> record = FirstZoneRecord(PayloadOf(source, kLvls));
    const std::vector<std::byte> spliced =
        FileOf({ { kSumm, kRoomSummary }, { kHead, {} }, { kLvls, LevelsHolding(record) }, { kSect, kNoSections } });
    const DecodedSavedGame decoded = DecodeSavedGame(spliced);
    ASSERT_TRUE(decoded.Saved.has_value()) << decoded.Error;
    ASSERT_EQ(decoded.Saved->Levels.size(), 1u);
    ASSERT_EQ(decoded.Saved->Levels[0].Zones.size(), 1u);
    const DurableZoneState& zone = decoded.Saved->Levels[0].Zones[0];
    EXPECT_EQ(zone.Zone, kCourtyard);
    EXPECT_TRUE(SameLists(zone.Lists, Example().Levels[0].Zones[0].Lists));
    EXPECT_TRUE(SameLists(zone.Held, Example().Levels[0].Zones[0].Held));
}

TEST(SavedGame, AKnownListAtANewerVersionMakesTheFileNewer)
{
    SavedGame saved = Example();
    saved.Levels[0].Zones[0].Lists = Lists({ Changed(kZoneListVersion + 1) });
    const DecodedSavedGame decoded = DecodeSavedGame(EncodeSavedGame(saved));
    EXPECT_FALSE(decoded.Saved.has_value());
    EXPECT_TRUE(decoded.Newer);
    EXPECT_NE(decoded.Error.find("changed"), std::string::npos) << decoded.Error;
}

TEST(SavedGame, AListAndAChunkThisBuildDoesNotKnowAreCarriedByteForByte)
{
    SavedGame saved = Example();
    saved.Levels[0].Zones[0].Lists = Lists({ { "occupied", 3, Bytes({ 5, 6, 7 }) } });
    saved.Carried.push_back(SavedGameChunk{ .Id = MakeFourCC('P', 'R', 'E', 'F'), .Version = 4,
                                            .Payload = Bytes({ 1 }), .InPrefix = true });
    const std::vector<std::byte> bytes = EncodeSavedGame(saved);
    const DecodedSavedGame decoded = DecodeSavedGame(bytes);
    ASSERT_TRUE(decoded.Saved.has_value()) << decoded.Error;
    EXPECT_EQ(EncodeSavedGame(*decoded.Saved), bytes);
    EXPECT_EQ(decoded.Saved->Carried.size(), 2u);
}

TEST(SavedGame, ACorruptOrCutShortFileIsRefused)
{
    std::vector<std::byte> bytes = EncodeSavedGame(Example());
    std::vector<std::byte> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2));
    EXPECT_FALSE(DecodeSavedGame(cut).Saved.has_value());

    bytes[bytes.size() / 2] ^= std::byte{ 0x40 };
    const DecodedSavedGame corrupt = DecodeSavedGame(bytes);
    EXPECT_FALSE(corrupt.Saved.has_value());
    EXPECT_FALSE(corrupt.Newer);
    EXPECT_FALSE(corrupt.Error.empty());
}

TEST(SavedGame, AKnownChunkFromANewerBuildIsRefusedAsNewer)
{
    HashedChunkFileWriter file(kMagic, kSavedGameFormatVersion);
    file.Chunk(kSumm, 1, [&] {
        return Serialize(file.Writer(), std::string(R"({"level":{"kind":"scene","name":"room"},"saved_at":5})"));
    });
    file.HashChunk(kHead);
    file.Chunk(kLvls, 2, [&] { return Serialize(file.Writer(), std::uint32_t{ 42 }); });
    const DecodedSavedGame decoded = DecodeSavedGame(file.Finish(kHash));
    EXPECT_FALSE(decoded.Saved.has_value());
    EXPECT_TRUE(decoded.Newer);
    EXPECT_NE(decoded.Error.find("LVLS"), std::string::npos) << decoded.Error;
}

TEST(SavedGame, AFormatFromANewerBuildIsRefusedAsNewer)
{
    const DecodedSavedGame decoded = DecodeSavedGame(FileOf({ { kSumm, kRoomSummary }, { kHead, {} } },
                                                             kSavedGameFormatVersion + 1));
    EXPECT_FALSE(decoded.Saved.has_value());
    EXPECT_TRUE(decoded.Newer);
}

TEST(SaveSlotFiles, ASlotsFileAndItsBackupAreWrittenApart)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "sencha_save_slots";
    std::filesystem::remove_all(root);
    const SaveSlotFiles files(root);
    const std::vector<std::byte> first{ std::byte{ 1 } };
    const std::vector<std::byte> second{ std::byte{ 2 } };
    ASSERT_TRUE(files.Write("slot_1", first));
    EXPECT_FALSE(files.ReadBackup("slot_1").has_value());
    ASSERT_TRUE(files.WriteBackup("slot_1", second));
    EXPECT_EQ(files.Read("slot_1"), first);
    EXPECT_EQ(files.ReadBackup("slot_1"), second);
    EXPECT_FALSE(files.Read("other").has_value());
    std::filesystem::remove_all(root);
}

TEST(SaveSlotFiles, ASlotIsLettersDigitsDashesAndUnderscores)
{
    EXPECT_TRUE(SaveSlotFiles::IsSlotName("default"));
    EXPECT_TRUE(SaveSlotFiles::IsSlotName("Slot-2_b"));
    EXPECT_FALSE(SaveSlotFiles::IsSlotName(""));
    EXPECT_FALSE(SaveSlotFiles::IsSlotName("../escape"));
    EXPECT_FALSE(SaveSlotFiles::IsSlotName("a b"));
    EXPECT_FALSE(SaveSlotFiles::IsSlotName(std::string(65, 'a')));
}
