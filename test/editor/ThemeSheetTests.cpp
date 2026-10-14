#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "problems/ProblemList.h"
#include "shell/ThemeSheet.h"
#include "ui/EditorThemeFile.h"
#include "ui/EditorUiStyle.h"

#include <core/console/ConsoleRegistry.h>

#include <gtest/gtest.h>

// The theme the editor.ui.theme cvar names is the palette, applied at the frame
// boundary; a theme that cannot be applied leaves the built-in palette and says
// why on the problems list.
namespace
{
class ThemeSheetTest : public ::testing::Test
{
protected:
    ThemeSheetTest() { ResetEditorTheme(); }
    ~ThemeSheetTest() override { ResetEditorTheme(); }

    void Choose(const std::string& theme)
    {
        (void)Console.SetCVar("editor.ui.theme", theme, ConsoleValueSource{}, ConsolePhase::GameplayStarted);
    }

    ConsoleRegistry Console;
    ProblemList Problems;
    ThemeSheet Sheet{ Console, Problems, SENCHA_EDITOR_THEME_DIR };
    Vec4 BuiltInAccent = EditorUi::Accent;
};
} // namespace

TEST_F(ThemeSheetTest, AThemeActionTakesEffectAtTheNextBoundary)
{
    ActionCatalog catalog;
    ActionContext context;
    Sheet.DefineActions(catalog, context);
    const ActionBinding* shudei = context.Find("theme.use.Shudei");
    ASSERT_NE(shudei, nullptr);
    ASSERT_NE(context.Find("theme.use.builtin"), nullptr);

    shudei->Invoke();
    EXPECT_FALSE(shudei->Checked());
    EXPECT_FLOAT_EQ(EditorUi::Accent.Z, BuiltInAccent.Z);

    Sheet.Update(nullptr);
    EXPECT_TRUE(shudei->Checked());
    EXPECT_NE(EditorUi::Accent.Z, BuiltInAccent.Z);
    EXPECT_TRUE(Problems.All().empty());
}

TEST_F(ThemeSheetTest, AThemeThatCannotLoadIsAProblemOverTheBuiltInPalette)
{
    Choose("Shudei");
    Sheet.Update(nullptr);
    Choose("no_such_theme");
    Sheet.Update(nullptr);
    EXPECT_FLOAT_EQ(EditorUi::Accent.Z, BuiltInAccent.Z);
    ASSERT_EQ(Problems.All().size(), 1u);

    Choose("");
    Sheet.Update(nullptr);
    EXPECT_TRUE(Problems.All().empty());
}
