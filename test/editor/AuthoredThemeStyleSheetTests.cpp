#include <gtest/gtest.h>

#include "ui/AuthoredThemeStyleSheet.h"
#include "ui/EditorThemeFile.h"
#include "ui/EditorUiStyle.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

// The active editor theme, written as a stylesheet authored surfaces import.
//
// Two rules hold this together, and both are easy to break by accident. The
// sheet carries colours and nothing else, because an authored document owns its
// own structure and a theme that moved things would make the same document lay
// out differently under different themes. And every role a shipped document
// names has to exist in it, because a class nobody defines is not an error --
// it is an element that silently goes unpainted.

namespace
{
#ifndef SENCHA_EDITOR_UI_DIR
#define SENCHA_EDITOR_UI_DIR "."
#endif

// Restores the palette whatever the test did to it: the entries are globals a
// theme load overwrites in place, so a test that changed one would change every
// test after it.
class ScopedPalette
{
public:
    ScopedPalette() { Saved = EditorUi::Accent; }
    ~ScopedPalette() { EditorUi::Accent = Saved; }
    ScopedPalette(const ScopedPalette&) = delete;
    ScopedPalette& operator=(const ScopedPalette&) = delete;

private:
    ImVec4 Saved{};
};

// Every `theme-*` class named by any shipped authored document.
[[nodiscard]] std::set<std::string> RolesUsedByDocuments()
{
    std::set<std::string> roles;
    for (const auto& entry : std::filesystem::directory_iterator(SENCHA_EDITOR_UI_DIR))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".rml")
            continue;
        std::ifstream file(entry.path());
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string text = buffer.str();

        for (std::size_t at = text.find("theme-"); at != std::string::npos;
             at = text.find("theme-", at + 1))
        {
            std::size_t end = at;
            while (end < text.size()
                   && (std::isalnum(static_cast<unsigned char>(text[end])) != 0
                       || text[end] == '-' || text[end] == '_'))
            {
                ++end;
            }
            roles.insert("." + text.substr(at, end - at));
        }
    }
    return roles;
}
} // namespace

TEST(AuthoredThemeStyleSheet, ItCarriesTheActivePaletteRatherThanALiteral)
{
    ScopedPalette restore;

    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    ASSERT_TRUE(ParseThemeColor("#FF8800FF", r, g, b, a));
    EditorUi::Accent = ImVec4(r, g, b, a);

    const std::string sheet = BuildAuthoredThemeStyleSheet();
    EXPECT_NE(sheet.find("#FF8800FF"), std::string::npos)
        << "a theme change did not reach the generated stylesheet";
}

TEST(AuthoredThemeStyleSheet, ItSetsNoGeometry)
{
    // The separation the whole mechanism rests on. A theme that set a width, a
    // padding or a font size would relayout every authored document on a switch
    // -- and would make a headless process, which supplies no theme, lay out
    // differently from the editor.
    static constexpr std::string_view kGeometry[] = {
        "width:", "height:", "margin", "padding", "top:", "left:", "right:", "bottom:",
        "font-size:", "line-height:", "display:", "position:", "border-width",
        "border-radius", "box-sizing", "overflow",
    };

    const std::string sheet = BuildAuthoredThemeStyleSheet();
    for (const std::string_view property : kGeometry)
    {
        EXPECT_EQ(sheet.find(property), std::string::npos)
            << "the generated theme sets '" << property << "', which is structure";
    }
}

TEST(AuthoredThemeStyleSheet, EveryRoleAShippedDocumentNamesIsDefined)
{
    // An undefined class is not an error in RCSS -- the element simply goes
    // unpainted -- so nothing but this would catch a document naming a role the
    // theme does not carry.
    const std::string sheet = BuildAuthoredThemeStyleSheet();
    const std::set<std::string> roles = RolesUsedByDocuments();
    ASSERT_FALSE(roles.empty()) << "no authored document was found to check against";

    for (const std::string& role : roles)
    {
        EXPECT_NE(sheet.find(role), std::string::npos)
            << "a shipped document uses '" << role << "', which the theme does not define";
    }
}

TEST(AuthoredThemeStyleSheet, ColoursAreWrittenInTheAuthoredSrgbEncoding)
{
    // The same encoding a theme file is saved in, because they come from the
    // same function: a stylesheet written in linear would land visibly wrong
    // through a swapchain that encodes on write.
    ScopedPalette restore;

    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    ASSERT_TRUE(ParseThemeColor("#2ED0EA", r, g, b, a));
    EditorUi::Accent = ImVec4(r, g, b, a);
    EXPECT_EQ(ThemeColorHex(EditorUi::Accent), "#2ED0EAFF");
}
