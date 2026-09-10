#include "ui/chrome/ChromeControls.h"
#include "ui/chrome/ChromeTile.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <gtest/gtest.h>

namespace
{
class ChromeControlsTests : public testing::Test
{
protected:
    void SetUp() override
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(640, 480);
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(600, 400));
        ImGui::Begin("controls");
    }
    void TearDown() override
    {
        ImGui::End();
        ImGui::EndFrame();
        ImGui::DestroyContext();
    }
};
}

TEST_F(ChromeControlsTests, HiddenButtonSuffixPreservesDistinctIdsWithoutWideningFace)
{
    EditorChrome::Button("Apply##one", "Apply##one", {}, EditorChrome::ButtonTone::Active);
    const float firstWidth = ImGui::GetItemRectSize().x;
    const ImGuiID firstId = ImGui::GetItemID();
    EditorChrome::Button("Apply##two", "Apply##two", {}, EditorChrome::ButtonTone::Normal);
    EXPECT_NE(firstId, ImGui::GetItemID());
    EXPECT_FLOAT_EQ(firstWidth, ImGui::GetItemRectSize().x);
    EXPECT_FLOAT_EQ(firstWidth, ImGui::CalcTextSize("Apply").x + ImGui::GetStyle().FramePadding.x * 2.0f);
}

TEST_F(ChromeControlsTests, TileAdvancesOneRowAndCarriesDisabledState)
{
    const float startY = ImGui::GetCursorScreenPos().y;
    const float rowHeight = 96 + ImGui::GetTextLineHeight();
    EditorChrome::Tile({ .Size = 96, .Label = "Material", .Disabled = true });
    EXPECT_FLOAT_EQ(ImGui::GetItemRectSize().y, rowHeight);
    EXPECT_FLOAT_EQ(ImGui::GetCursorScreenPos().y - startY, rowHeight + ImGui::GetStyle().ItemSpacing.y);
    EXPECT_NE(ImGui::GetCurrentContext()->LastItemData.ItemFlags & ImGuiItemFlags_Disabled, 0);
    EXPECT_FALSE(ImGui::BeginDragDropSource());
}

TEST_F(ChromeControlsTests, NoninteractiveTileHasNoWidgetId)
{
    EditorChrome::Tile({ .Size = 96, .Interactive = false });
    EXPECT_EQ(ImGui::GetItemID(), 0u);
    EXPECT_FLOAT_EQ(ImGui::GetItemRectSize().y, 96);
}

TEST_F(ChromeControlsTests, OpenComboKeepsPopupAndParentIdStacksBalanced)
{
    ImGuiWindow* parent = ImGui::GetCurrentWindow();
    const int parentDepth = parent->IDStack.Size;
    const ImGuiID id = parent->GetID("##combo");
    ImGui::OpenPopupEx(ImHashStr("##ComboPopup", 0, id));
    ImGui::SetNextItemWidth(120);
    ASSERT_TRUE(EditorChrome::BeginCombo("##combo", "Preview"));
    EXPECT_NE(ImGui::GetCurrentWindow(), parent);
    ImGui::Selectable("Choice");
    EditorChrome::EndCombo();
    EXPECT_EQ(ImGui::GetCurrentWindow(), parent);
    EXPECT_EQ(parent->IDStack.Size, parentDepth);
}
