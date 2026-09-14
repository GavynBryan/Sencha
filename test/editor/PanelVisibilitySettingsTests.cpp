#include "ui/IEditorPanel.h"
#include "ui/PanelVisibilitySettings.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

// A remembered panel's visibility rides the ImGui layout file under its
// declared id, so a title can change without a user's layout changing; a
// session-only panel keeps whatever its owner set.
namespace
{
class StubPanel : public IEditorPanel
{
public:
    StubPanel(std::string title, std::string id, PanelVisibilityPolicy policy)
        : Title(std::move(title))
        , Id(std::move(id))
        , Policy(policy)
    {
    }
    std::string_view GetTitle() const override { return Title; }
    PanelPersistence GetPersistence() const override { return { Id, Policy }; }
    void OnDraw() override {}

    std::string Title;
    std::string Id;
    PanelVisibilityPolicy Policy;
};

class PanelVisibilitySettingsTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        Panels.push_back(std::make_unique<StubPanel>("TOOLS", "tools", PanelVisibilityPolicy::Remembered));
        Panels.push_back(std::make_unique<StubPanel>("CONSOLE", "console", PanelVisibilityPolicy::SessionOnly));
        Settings.Register(Panels);
    }
    void TearDown() override { ImGui::DestroyContext(); }

    [[nodiscard]] StubPanel& Tools() { return static_cast<StubPanel&>(*Panels[0]); }
    [[nodiscard]] StubPanel& Console() { return static_cast<StubPanel&>(*Panels[1]); }
    [[nodiscard]] std::string Saved()
    {
        size_t size = 0;
        const char* text = ImGui::SaveIniSettingsToMemory(&size);
        return std::string(text, size);
    }

    std::vector<std::unique_ptr<IEditorPanel>> Panels;
    PanelVisibilitySettings Settings;
};
}

TEST_F(PanelVisibilitySettingsTest, ARememberedPanelIsRestoredAndASessionOnlyOneIsNot)
{
    ImGui::LoadIniSettingsFromMemory("[EditorPanels][tools]\nVisible=0\n\n[EditorPanels][console]\nVisible=0\n\n");
    Settings.Apply();
    EXPECT_FALSE(Tools().IsVisible());
    EXPECT_TRUE(Console().IsVisible());
}

TEST_F(PanelVisibilitySettingsTest, OnlyRememberedPanelsAreWritten)
{
    Tools().SetVisible(false);
    Console().SetVisible(false);
    const std::string saved = Saved();
    EXPECT_NE(saved.find("[EditorPanels][tools]\nVisible=0"), std::string::npos) << saved;
    EXPECT_EQ(saved.find("console"), std::string::npos) << saved;
}

TEST_F(PanelVisibilitySettingsTest, ATitleChangeKeepsTheSetting)
{
    ImGui::LoadIniSettingsFromMemory("[EditorPanels][tools]\nVisible=0\n\n");
    Tools().Title = "TOOL PALETTE";
    Settings.Apply();
    EXPECT_FALSE(Tools().IsVisible());
}

TEST_F(PanelVisibilitySettingsTest, TrackMarksTheFileDirtyOnlyWhenVisibilityChanges)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    Settings.Apply();
    g.SettingsDirtyTimer = 0.0f;
    Settings.Track();
    EXPECT_FLOAT_EQ(g.SettingsDirtyTimer, 0.0f);

    Tools().SetVisible(false);
    Settings.Track();
    EXPECT_GT(g.SettingsDirtyTimer, 0.0f);

    g.SettingsDirtyTimer = 0.0f;
    Console().SetVisible(false);
    Settings.Track();
    EXPECT_FLOAT_EQ(g.SettingsDirtyTimer, 0.0f);
}
