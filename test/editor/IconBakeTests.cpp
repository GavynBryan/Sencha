#include "ui/chrome/IconDraw.h"

#include <imgui.h>
#include <gtest/gtest.h>

namespace
{
// A context with the default font and no renderer: the bake writes into the
// atlas's CPU pixels, so it needs no GPU.
class IconBakeTests : public testing::Test
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
    }
    void TearDown() override { ImGui::DestroyContext(); }

    // Runs one frame that draws every icon at a few sizes; the fallback
    // safety check is that nothing asserts.
    static void DrawEveryIcon()
    {
        ImGuiIO& io = ImGui::GetIO();
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImGui::NewFrame();
        ImGui::Begin("icons");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < static_cast<int>(IconId::Count); ++i)
            for (float side : { 9.0f, 15.0f, 60.0f })
                EditorChrome::DrawIcon(dl, static_cast<IconId>(i), ImVec2(0, 0), ImVec2(side, side), 0xFFFFFFFFu);
        ImGui::End();
        ImGui::EndFrame();
    }
};
}

TEST_F(IconBakeTests, EveryIconFileBakesToSomething)
{
    EXPECT_EQ(EditorChrome::BakeIcons(*ImGui::GetIO().Fonts, 1.0f), static_cast<int>(IconId::Count) - 1);
    EXPECT_NE(ImGui::GetIO().Fonts->TexPixelsAlpha8, nullptr);
    DrawEveryIcon();
}

TEST_F(IconBakeTests, DrawingWithoutABakeFallsBackToTheGlyph)
{
    DrawEveryIcon();
}

TEST_F(IconBakeTests, ABakeFromAnEarlierContextIsNotReused)
{
    EXPECT_GT(EditorChrome::BakeIcons(*ImGui::GetIO().Fonts, 1.0f), 0);
    ImGui::DestroyContext();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(640, 480);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    DrawEveryIcon();
}
