#include <core/io/FileBytes.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/FourCC.h>
#include <core/serialization/HashedChunkFile.h>
#include <core/serialization/Serialize.h>
#include <save/SaveSlotFiles.h>
#include <save/SavedGame.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    SavedGame Example()
    {
        SavedGame saved;
        saved.Summary.Level = LevelReference{ .Kind = LevelKind::World, .Name = "garden" };
        saved.Summary.Focus = ZoneId{ 0x1234abcd5678ef00 };
        saved.Summary.SavedAtUnixSeconds = 1'763'000'000;
        DurableZoneState zone;
        zone.Zone = ZoneId{ 0x1234abcd5678ef00 };
        zone.Destroyed = { PersistentEntityId{ 0x10 }, PersistentEntityId{ 0x20 } };
        zone.Components.push_back(DurableComponentState{ .Entity = PersistentEntityId{ 0x30 },
                                                         .Type = ComponentTypeId{ 0x9999 },
                                                         .Version = 1,
                                                         .Bytes = { std::byte{ 7 }, std::byte{ 0 }, std::byte{ 9 } } });
        saved.Zones.push_back(zone);
        saved.Sections.push_back(SavedGameSection{
            .Name = "game.notes", .Version = 2, .Data = JsonValue(JsonValue::Object{ { "seen", JsonValue(true) } }) });
        return saved;
    }

    void ExpectSame(const SavedGame& actual, const SavedGame& expected)
    {
        EXPECT_EQ(actual.Summary, expected.Summary);
        EXPECT_EQ(actual.Zones, expected.Zones);
        ASSERT_EQ(actual.Sections.size(), expected.Sections.size());
        for (std::size_t i = 0; i < actual.Sections.size(); ++i)
        {
            EXPECT_EQ(actual.Sections[i].Name, expected.Sections[i].Name);
            EXPECT_EQ(actual.Sections[i].Version, expected.Sections[i].Version);
            EXPECT_EQ(JsonStringify(actual.Sections[i].Data), JsonStringify(expected.Sections[i].Data));
        }
    }

    const std::filesystem::path kFixture = SENCHA_REPO_ROOT "/test/fixtures/saves/example.v1.ssav";
}

TEST(SavedGame, WhatIsEncodedDecodesTheSame)
{
    const std::vector<std::byte> bytes = EncodeSavedGame(Example());
    ASSERT_FALSE(bytes.empty());
    std::string error;
    const std::optional<SavedGame> decoded = DecodeSavedGame(bytes, &error);
    ASSERT_TRUE(decoded.has_value()) << error;
    ExpectSame(*decoded, Example());
}

// The first format's bytes, checked in: a later build must still read them.
// SENCHA_WRITE_SAVE_FIXTURE=1 writes them again, which only a format change
// that keeps reading this one may do.
TEST(SavedGame, TheFirstFormatsFixtureStillReads)
{
    if (std::getenv("SENCHA_WRITE_SAVE_FIXTURE") != nullptr)
    {
        ASSERT_TRUE(WriteFileBytes(kFixture, EncodeSavedGame(Example())));
    }
    std::vector<std::byte> bytes;
    ASSERT_TRUE(ReadFileBytes(kFixture, bytes)) << kFixture;
    std::string error;
    const std::optional<SavedGame> decoded = DecodeSavedGame(bytes, &error);
    ASSERT_TRUE(decoded.has_value()) << error;
    ExpectSame(*decoded, Example());
}

TEST(SavedGame, ACorruptOrCutShortFileIsRefused)
{
    std::vector<std::byte> bytes = EncodeSavedGame(Example());
    std::vector<std::byte> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2));
    std::string error;
    EXPECT_FALSE(DecodeSavedGame(cut, &error).has_value());

    bytes[bytes.size() / 2] ^= std::byte{ 0x40 };
    EXPECT_FALSE(DecodeSavedGame(bytes, &error).has_value());
    EXPECT_FALSE(error.empty());
}

TEST(SavedGame, AChunkThisBuildDoesNotKnowIsSkipped)
{
    HashedChunkFileWriter file(MakeFourCC('S', 'S', 'A', 'V'), kSavedGameFormatVersion);
    std::string error;
    file.Chunk(MakeFourCC('S', 'U', 'M', 'M'), 1, [&] {
        return Serialize(file.Writer(),
                         std::string(R"({"level":{"kind":"scene","name":"room"},"saved_at":5})"));
    });
    file.Chunk(MakeFourCC('F', 'U', 'T', 'R'), 1, [&] { return Serialize(file.Writer(), std::uint32_t{ 42 }); });
    const std::optional<SavedGame> decoded = DecodeSavedGame(file.Finish(MakeFourCC('H', 'A', 'S', 'H')), &error);
    ASSERT_TRUE(decoded.has_value()) << error;
    EXPECT_EQ(decoded->Summary.Level.Name, "room");
    EXPECT_TRUE(decoded->Zones.empty());
}

TEST(SaveSlotFiles, AWriteKeepsTheFileItReplacesAsTheBackup)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "sencha_save_slots";
    std::filesystem::remove_all(root);
    const SaveSlotFiles files(root);
    const std::vector<std::byte> first{ std::byte{ 1 } };
    const std::vector<std::byte> second{ std::byte{ 2 } };
    ASSERT_TRUE(files.Write("slot_1", first));
    ASSERT_TRUE(files.Write("slot_1", second));
    EXPECT_EQ(files.Read("slot_1"), second);
    EXPECT_EQ(files.ReadBackup("slot_1"), first);
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
