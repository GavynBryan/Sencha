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

TEST(EditorThemeFile, MetricsApplyAndProblemsWarn)
{
    const float defaultChamfer = EditorUi::Metrics.Chamfer;
    ASSERT_NE(defaultChamfer, 9.5f);

    const auto path = WriteTempTheme(
        "sencha_theme_metrics_test.json",
        R"({ "colors": {}, "metrics": { "chamfer": 9.5, "rail_height": "six", "not_a_metric": 1 } })");
    std::string error;
    ASSERT_TRUE(LoadEditorTheme(path, &error));
    EXPECT_FLOAT_EQ(EditorUi::Metrics.Chamfer, 9.5f);
    // A bad value and an unknown key each warn, and neither aborts the load.
    EXPECT_NE(error.find("rail_height"), std::string::npos) << error;
    EXPECT_NE(error.find("not_a_metric"), std::string::npos) << error;

    ResetEditorTheme();
    EXPECT_FLOAT_EQ(EditorUi::Metrics.Chamfer, defaultChamfer);
    std::filesystem::remove(path);
}

TEST(EditorThemeFile, MetricsOnlyFileLoads)
{
    const auto path = WriteTempTheme("sencha_theme_metrics_only_test.json",
                                     R"({ "metrics": { "screw_radius": 4 } })");
    std::string error;
    ASSERT_TRUE(LoadEditorTheme(path, &error)) << error;
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_FLOAT_EQ(EditorUi::Metrics.ScrewRadius, 4.0f);

    ResetEditorTheme();
    std::filesystem::remove(path);
}

TEST(EditorThemeFile, ColorsOnlyFileRevertsMetricsToDefaults)
{
    // A theme describes the whole look: a file with no metrics section leaves
    // the built-in metrics, not the previous theme's.
    const float defaultChamfer = EditorUi::Metrics.Chamfer;
    EditorUi::Metrics.Chamfer = 42.0f;

    const auto path = WriteTempTheme("sencha_theme_colors_only_test.json",
                                     R"({ "colors": { "accent": "#FFFFFF" } })");
    std::string error;
    ASSERT_TRUE(LoadEditorTheme(path, &error)) << error;
    EXPECT_FLOAT_EQ(EditorUi::Metrics.Chamfer, defaultChamfer);

    ResetEditorTheme();
    std::filesystem::remove(path);
}

TEST(EditorThemeFile, NonObjectMetricsWarnsButLoads)
{
    const auto path = WriteTempTheme("sencha_theme_metrics_shape_test.json",
                                     R"({ "colors": { "accent": "#FFFFFF" }, "metrics": 5 })");
    std::string error;
    EXPECT_TRUE(LoadEditorTheme(path, &error));
    EXPECT_NE(error.find("metrics"), std::string::npos) << error;

    ResetEditorTheme();
    std::filesystem::remove(path);
}

TEST(EditorThemeFile, SaveRoundTripsColorsAndMetrics)
{
    const auto edited = WriteTempTheme(
        "sencha_theme_roundtrip_src_test.json",
        R"({ "colors": { "accent": "#FF0080", "selected_outline": "#FFB347" }, "metrics": { "rail_height": 11, "glow_alpha": 0.5 } })");
    std::string error;
    ASSERT_TRUE(LoadEditorTheme(edited, &error)) << error;

    const auto saved = std::filesystem::temp_directory_path() / "sencha_theme_roundtrip_out_test.json";
    ASSERT_TRUE(SaveEditorTheme(saved, &error)) << error;

    ResetEditorTheme();
    EXPECT_NE(EditorUi::Metrics.RailHeight, 11.0f);

    ASSERT_TRUE(LoadEditorTheme(saved, &error)) << error;
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_FLOAT_EQ(EditorUi::Metrics.RailHeight, 11.0f);
    EXPECT_FLOAT_EQ(EditorUi::Metrics.GlowAlpha, 0.5f);
    EXPECT_NEAR(EditorUi::Accent.x, 1.0f, 1e-4f);
    EXPECT_NEAR(EditorUi::Accent.z, 0.2158f, 1e-3f);
    EXPECT_NEAR(EditorUi::SelectedOutline.x, 1.0f, 1e-4f);

    ResetEditorTheme();
    std::filesystem::remove(edited);
    std::filesystem::remove(saved);
}

TEST(EditorThemeFile, BundledThemesLoadWithoutProblems)
{
    // Every theme shipped under editor/themes names only keys the palette and
    // metrics tables know, so a bundled theme never warns on selection.
    for (const char* name : { "workstation.json", "dark_teal.json", "Shudei.json" })
    {
        const std::filesystem::path path = std::filesystem::path(SENCHA_EDITOR_THEME_DIR) / name;
        std::string error;
        EXPECT_TRUE(LoadEditorTheme(path, &error)) << error;
        EXPECT_TRUE(error.empty()) << error;
    }
    ResetEditorTheme();
}

TEST(EditorThemeFile, DecorStringsLoadSilenceAndRoundTrip)
{
    const auto path = WriteTempTheme(
        "sencha_theme_decor_test.json",
        R"({ "decor": { "hierarchy_empty": "VOID", "status_tagline": "", "scene_browser_empty": "ONE\nTWO", "not_a_slot": "x", "console_empty": 3 } })");
    std::string error;
    ASSERT_TRUE(LoadEditorTheme(path, &error));
    EXPECT_EQ(EditorUi::Decor.HierarchyEmpty, "VOID");
    EXPECT_TRUE(EditorUi::Decor.StatusTagline.empty());
    EXPECT_EQ(EditorUi::Decor.SceneBrowserEmpty, "ONE\nTWO");
    EXPECT_NE(error.find("not_a_slot"), std::string::npos) << error;
    EXPECT_NE(error.find("console_empty"), std::string::npos) << error;
    // Untouched slots keep the built-in copy.
    EXPECT_FALSE(EditorUi::Decor.ToolPropertiesIdle.empty());

    const auto saved = std::filesystem::temp_directory_path() / "sencha_theme_decor_out_test.json";
    ASSERT_TRUE(SaveEditorTheme(saved, &error)) << error;
    ResetEditorTheme();
    EXPECT_EQ(EditorUi::Decor.HierarchyEmpty, "GEOMETRY");
    error.clear();
    ASSERT_TRUE(LoadEditorTheme(saved, &error)) << error;
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_EQ(EditorUi::Decor.HierarchyEmpty, "VOID");
    EXPECT_EQ(EditorUi::Decor.SceneBrowserEmpty, "ONE\nTWO");
    EXPECT_TRUE(EditorUi::Decor.StatusTagline.empty());

    ResetEditorTheme();
    std::filesystem::remove(path);
    std::filesystem::remove(saved);
}
