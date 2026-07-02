#include "ui/EditorThemeFile.h"
#include "ui/EditorUiStyle.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>

namespace
{
std::filesystem::path WriteTempTheme(const char* name, const char* json)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream file(path);
    file << json;
    return path;
}
}

TEST(EditorThemeFile, ParsesHexAsSrgbAndLinearizes)
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
    ASSERT_TRUE(ParseThemeColor("#FF0080", r, g, b, a));
    // 0xFF -> sRGB 1.0 -> linear 1.0; 0x80 -> sRGB ~0.502 -> linear ~0.216.
    EXPECT_NEAR(r, 1.0f, 1e-4f);
    EXPECT_NEAR(g, 0.0f, 1e-4f);
    EXPECT_NEAR(b, 0.2158f, 1e-3f);
    EXPECT_FLOAT_EQ(a, 1.0f);

    ASSERT_TRUE(ParseThemeColor("#00000080", r, g, b, a));
    EXPECT_NEAR(a, 128.0f / 255.0f, 1e-4f); // alpha stays linear

    EXPECT_FALSE(ParseThemeColor("FF0080", r, g, b, a));
    EXPECT_FALSE(ParseThemeColor("#F80", r, g, b, a));
    EXPECT_FALSE(ParseThemeColor("#GG0080", r, g, b, a));
}

TEST(EditorThemeFile, MissingKeysKeepDefaultsAndKnownKeysApply)
{
    const ImVec4 defaultAccent = EditorUi::Accent;
    const ImVec4 defaultBorder = EditorUi::Border;

    const auto path = WriteTempTheme("sencha_theme_test.json",
                                     R"({ "colors": { "accent": "#FFFFFF" } })");
    std::string error;
    ASSERT_TRUE(LoadEditorTheme(path, &error)) << error;
    EXPECT_TRUE(error.empty()) << error;

    EXPECT_NEAR(EditorUi::Accent.x, 1.0f, 1e-4f);
    EXPECT_NEAR(EditorUi::Accent.y, 1.0f, 1e-4f);
    // Untouched entry keeps the built-in default.
    EXPECT_FLOAT_EQ(EditorUi::Border.x, defaultBorder.x);

    EditorUi::Accent = defaultAccent; // restore for other tests
    std::filesystem::remove(path);
}

TEST(EditorThemeFile, UnknownKeysWarnButDoNotFail)
{
    const auto path = WriteTempTheme("sencha_theme_unknown_test.json",
                                     R"({ "colors": { "not_a_color": "#FFFFFF" } })");
    std::string error;
    EXPECT_TRUE(LoadEditorTheme(path, &error));
    EXPECT_FALSE(error.empty());
    std::filesystem::remove(path);
}

TEST(EditorThemeFile, MalformedFileFails)
{
    const auto path = WriteTempTheme("sencha_theme_bad_test.json", "{ not json");
    std::string error;
    EXPECT_FALSE(LoadEditorTheme(path, &error));
    EXPECT_FALSE(error.empty());
    std::filesystem::remove(path);
}
