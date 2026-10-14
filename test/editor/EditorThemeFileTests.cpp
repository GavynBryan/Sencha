#include "ui/EditorThemeFile.h"
#include "ui/EditorUiStyle.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <utility>

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
    const Vec4 defaultBorder = EditorUi::Border;

    const auto path = WriteTempTheme("sencha_theme_test.json",
                                     R"({ "colors": { "accent": "#FFFFFF" } })");
    std::string error;
    ASSERT_TRUE(LoadEditorTheme(path, &error)) << error;
    EXPECT_TRUE(error.empty()) << error;

    EXPECT_NEAR(EditorUi::Accent.X, 1.0f, 1e-4f);
    EXPECT_NEAR(EditorUi::Accent.Y, 1.0f, 1e-4f);
    // Untouched entry keeps the built-in default.
    EXPECT_FLOAT_EQ(EditorUi::Border.X, defaultBorder.X);

    ResetEditorTheme();
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

TEST(EditorThemeFile, AFileWithoutColorsIsNotATheme)
{
    const auto path = WriteTempTheme("sencha_theme_no_colors_test.json", R"({ "metrics": { "chamfer": 4 } })");
    std::string error;
    EXPECT_FALSE(LoadEditorTheme(path, &error));
    EXPECT_NE(error.find("colors"), std::string::npos) << error;
    std::filesystem::remove(path);
}

TEST(EditorThemeFile, RetiredSectionsLoadWithoutProblems)
{
    const auto path = WriteTempTheme(
        "sencha_theme_retired_test.json",
        R"({ "colors": { "accent": "#FFFFFF" }, "metrics": { "chamfer": 4 }, "decor": { "hierarchy_empty": "VOID" } })");
    std::string error;
    ASSERT_TRUE(LoadEditorTheme(path, &error)) << error;
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_NEAR(EditorUi::Accent.X, 1.0f, 1e-4f);
    ResetEditorTheme();
    std::filesystem::remove(path);
}

TEST(EditorThemeFile, ALoadStartsFromTheBuiltInPalette)
{
    const Vec4 builtInBorder = EditorUi::Border;
    const auto first = WriteTempTheme("sencha_theme_first_test.json", R"({ "colors": { "border": "#FFFFFF" } })");
    const auto second = WriteTempTheme("sencha_theme_second_test.json", R"({ "colors": { "accent": "#FFFFFF" } })");
    ASSERT_TRUE(LoadEditorTheme(first, nullptr));
    ASSERT_TRUE(LoadEditorTheme(second, nullptr));
    EXPECT_FLOAT_EQ(EditorUi::Border.X, builtInBorder.X);
    ResetEditorTheme();
    std::filesystem::remove(first);
    std::filesystem::remove(second);
}

TEST(EditorThemeFile, HexRoundTripsThroughTheLinearPalette)
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
    ASSERT_TRUE(ParseThemeColor("#FF008040", r, g, b, a));
    EXPECT_EQ(ThemeColorHex(Vec4(r, g, b, a)), "#FF008040");
}

TEST(EditorThemeFile, BundledThemesLoadWithoutProblems)
{
    // Every theme shipped under editor/themes names only palette keys, so a
    // bundled theme never warns on selection.
    for (const char* name : { "workstation.json", "dark_teal.json", "Shudei.json" })
    {
        const std::filesystem::path path = std::filesystem::path(SENCHA_EDITOR_THEME_DIR) / name;
        std::string error;
        EXPECT_TRUE(LoadEditorTheme(path, &error)) << error;
        EXPECT_TRUE(error.empty()) << error;
    }
    ResetEditorTheme();
}

namespace
{
float Luminance(const Vec4& linear)
{
    return 0.2126f * linear.X + 0.7152f * linear.Y + 0.0722f * linear.Z;
}

float Contrast(const Vec4& a, const Vec4& b)
{
    const float la = Luminance(a);
    const float lb = Luminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}
} // namespace

TEST(EditorThemeFile, EveryBundledThemesTextReadsOnEverySurfaceTextSitsOn)
{
    // 4.5:1 is the floor for small text; the editor's text is all small.
    for (const char* name : { "workstation.json", "dark_teal.json", "Shudei.json" })
    {
        const std::filesystem::path path = std::filesystem::path(SENCHA_EDITOR_THEME_DIR) / name;
        ASSERT_TRUE(LoadEditorTheme(path, nullptr)) << name;
        const std::pair<const char*, Vec4> surfaces[] = {
            { "window", EditorUi::WindowBg }, { "panel", EditorUi::PanelBg },   { "header", EditorUi::HeaderBg },
            { "button", EditorUi::ButtonBg }, { "plate", EditorUi::MetalBase },
        };
        for (const auto& [surface, color] : surfaces)
        {
            EXPECT_GE(Contrast(EditorUi::TextPrimary, color), 4.5f) << name << ": primary text on " << surface;
            EXPECT_GE(Contrast(EditorUi::TextDim, color), 4.5f) << name << ": secondary text on " << surface;
        }
    }
    ResetEditorTheme();
}
