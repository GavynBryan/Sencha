#include "input/KeymapFile.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

TEST(KeymapFile, ParsesModifierChords)
{
    const auto chord = ParseKeyChord("Ctrl+Shift+Z");
    ASSERT_TRUE(chord.has_value());
    EXPECT_EQ(chord->Key, SDLK_Z);
    EXPECT_TRUE(chord->Mods.Ctrl);
    EXPECT_TRUE(chord->Mods.Shift);
    EXPECT_FALSE(chord->Mods.Alt);

    const auto bare = ParseKeyChord("4");
    ASSERT_TRUE(bare.has_value());
    EXPECT_EQ(bare->Key, SDLK_4);
    EXPECT_FALSE(bare->Mods.Ctrl);

    const auto named = ParseKeyChord("Escape");
    ASSERT_TRUE(named.has_value());
    EXPECT_EQ(named->Key, SDLK_ESCAPE);
}

TEST(KeymapFile, RejectsMalformedChords)
{
    EXPECT_FALSE(ParseKeyChord("").has_value());
    EXPECT_FALSE(ParseKeyChord("Ctrl+").has_value());
    EXPECT_FALSE(ParseKeyChord("NotAKeyName").has_value());
    EXPECT_FALSE(ParseKeyChord("Z+Ctrl").has_value()); // key must be last
}

TEST(KeymapFile, LoadsOverridesAndSkipsBadEntries)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "sencha_keymap_test.json";
    {
        std::ofstream file(path);
        file << R"({ "edit.undo": "Ctrl+U", "edit.redo": "garbage+++" })";
    }

    std::string error;
    const auto overrides = LoadKeymapOverrides(path, &error);
    ASSERT_EQ(overrides.size(), 1u);
    EXPECT_EQ(overrides.at("edit.undo").Key, SDLK_U);
    EXPECT_TRUE(overrides.at("edit.undo").Mods.Ctrl);
    EXPECT_FALSE(error.empty()); // the bad entry is reported

    std::filesystem::remove(path);
}

TEST(KeymapFile, MissingFileMeansDefaults)
{
    std::string error;
    const auto overrides = LoadKeymapOverrides("does_not_exist_keymap.json", &error);
    EXPECT_TRUE(overrides.empty());
    EXPECT_TRUE(error.empty());
}
