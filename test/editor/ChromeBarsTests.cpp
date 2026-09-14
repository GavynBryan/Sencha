#include "ui/EditorUiStyle.h"
#include "ui/chrome/ChromeBars.h"
#include "ui/chrome/ChromeHeader.h"

#include <imgui.h>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
// A context with no window open: the bar tests each open the host they need,
// and the caption tests need the main menu bar rather than an ordinary window.
class ChromeBarsTests : public testing::Test
{
protected:
    void SetUp() override
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        // The chrome metrics are process-wide and the theme tests write them.
        EditorUi::Metrics = EditorUi::ChromeMetrics{};
        EditorUi::UiScale = 1.0f;
        ImGui::NewFrame();
    }

    void TearDown() override
    {
        ImGui::EndFrame();
        ImGui::DestroyContext();
    }
};
}

TEST_F(ChromeBarsTests, EmptyModuleTakesNoSpaceInAVerticalHost)
{
    ImGui::Begin("host");
    const ImVec2 origin(40.0f, 60.0f);
    ImGui::SetCursorScreenPos(origin);
    {
        EditorChrome::ModuleScope module("empty");
    }
    // A host opens a module for a tool that may draw nothing into it. That
    // module must leave the cursor exactly where it began, so the group ImGui
    // emits for it never pushes the row that follows off its line.
    EXPECT_FLOAT_EQ(ImGui::GetCursorScreenPos().x, origin.x);
    EXPECT_FLOAT_EQ(ImGui::GetCursorScreenPos().y, origin.y);

    {
        EditorChrome::ModuleScope module("full");
        ImGui::Dummy(ImVec2(24.0f, 24.0f));
    }
    EXPECT_GT(ImGui::GetCursorScreenPos().y, origin.y);
    ImGui::End();
}

TEST_F(ChromeBarsTests, EmptyModuleTakesNoSpaceInAHorizontalRow)
{
    ImGui::Begin("host", nullptr, ImGuiWindowFlags_MenuBar);
    ASSERT_TRUE(ImGui::BeginMenuBar());
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    {
        EditorChrome::ModuleScope module("empty");
    }
    EXPECT_FLOAT_EQ(ImGui::GetCursorScreenPos().x, origin.x);
    EXPECT_FLOAT_EQ(ImGui::GetCursorScreenPos().y, origin.y);

    // The next module starts where the empty one did, so a row of modules is
    // laid out the same whether or not the first of them drew anything.
    {
        EditorChrome::ModuleScope module("full");
        ImGui::Dummy(ImVec2(24.0f, 20.0f));
    }
    EXPECT_FLOAT_EQ(ImGui::GetItemRectMin().x, origin.x);
    ImGui::EndMenuBar();
    ImGui::End();
}

TEST_F(ChromeBarsTests, EveryFinishLaysOutTheSameChassis)
{
    ImGui::Begin("bar");
    const ImVec2 mn(0.0f, 0.0f);
    const ImVec2 mx(800.0f, 39.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    EditorChrome::BarSurface solid;
    const EditorChrome::BarRects reference =
        EditorChrome::BarFrame(dl, mn, mx, EditorChrome::BarEdge::Bottom, 23.0f, solid);

    // A theme's surface is paint: it may not move a rim, a cap, the channel, or
    // the lane a host seats its controls on.
    for (const EditorUi::BarFinish finish : { EditorUi::BarFinish::GradientX, EditorUi::BarFinish::GradientY,
                                              EditorUi::BarFinish::Texture })
    {
        EditorChrome::BarSurface surface;
        surface.Finish = finish;
        const EditorChrome::BarRects rects =
            EditorChrome::BarFrame(dl, mn, mx, EditorChrome::BarEdge::Bottom, 23.0f, surface);
        EXPECT_FLOAT_EQ(rects.LaneMin.y, reference.LaneMin.y);
        EXPECT_FLOAT_EQ(rects.LaneMax.y, reference.LaneMax.y);
        EXPECT_FLOAT_EQ(rects.ChannelMin.x, reference.ChannelMin.x);
        EXPECT_FLOAT_EQ(rects.ChannelMax.x, reference.ChannelMax.x);
        EXPECT_FLOAT_EQ(rects.TopRimMax.y, reference.TopRimMax.y);
        EXPECT_EQ(rects.HasCaps, reference.HasCaps);
    }
    ImGui::End();
}

TEST_F(ChromeBarsTests, HeaderRowWidthHoldsWhatTheRowDraws)
{
    ImGui::Begin("bar");
    const EditorChrome::HeaderRowSpec spec{ .Style = PanelStyle::Standard };
    const float height = 29.0f;
    const float title = 120.0f;
    const float rule = 36.0f;
    const float gap = EditorUi::Px(6.0f);
    const float width = EditorChrome::HeaderRowWidth(spec, height, title, rule, gap);

    // A plate of that width lays out with its cap, its whole title, and a rule
    // at least as long as the caller asked for -- which is what lets the shell
    // size its nameplate without copying the packing.
    const EditorChrome::HeaderRegions regions = EditorChrome::DrawHeaderRow(
        ImGui::GetWindowDrawList(), ImVec2(0.0f, 0.0f), ImVec2(width, height), "KYUSU",
        EditorUi::TextRole::Body, EditorChrome::HeaderState{}, spec);
    EXPECT_TRUE(regions.HasCap);
    EXPECT_TRUE(regions.HasLine);
    EXPECT_GE(regions.LineMax.x - regions.LineMin.x, rule - 0.5f);
    ImGui::End();
}

TEST_F(ChromeBarsTests, MenuBarStripWidthIsTheBayThatHoldsTheMenus)
{
    ImGui::Begin("host", nullptr, ImGuiWindowFlags_MenuBar);
    ASSERT_TRUE(ImGui::BeginMenuBar());

    static const char* const kLabels[] = { "File", "Edit", "View" };
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float bayWidth = EditorChrome::MenuBarStripWidth(kLabels) + spacing * 2.0f;

    // Placed the way the caption places them: the bay's left edge, plus the
    // half spacing a horizontal menu offsets itself by.
    const float bayMin = ImGui::GetCursorScreenPos().x + 20.0f;
    ImGui::SetCursorScreenPos(ImVec2(bayMin + std::trunc(spacing * 0.5f), ImGui::GetCursorScreenPos().y));

    float firstMin = 0.0f;
    float lastMax = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        if (ImGui::BeginMenu(kLabels[i]))
            ImGui::EndMenu();
        if (i == 0)
            firstMin = ImGui::GetItemRectMin().x;
        lastMax = ImGui::GetItemRectMax().x;
    }

    // The bay a user sees and the strip they can click are the same rectangle.
    // This pins the spacing arithmetic mirrored from BeginMenu against the
    // ImGui the editor builds with, so a version bump fails here.
    EXPECT_FLOAT_EQ(firstMin, bayMin);
    EXPECT_FLOAT_EQ(lastMax, bayMin + bayWidth);

    ImGui::EndMenuBar();
    ImGui::End();
}

TEST_F(ChromeBarsTests, MenuItemsStayCenteredOnThePaddedFirstAppend)
{
    // The caption's first append, reproduced: the pushed padding both makes the
    // bar taller and centers the menus in it.
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(style.FramePadding.x, style.FramePadding.y + EditorChrome::BarLaneInset()));
    const bool open = ImGui::BeginMainMenuBar();
    ImGui::PopStyleVar();
    ASSERT_TRUE(open);

    const float barMinY = ImGui::GetWindowPos().y;
    const float barMaxY = barMinY + ImGui::GetWindowSize().y;
    if (ImGui::BeginMenu("File"))
        ImGui::EndMenu();
    const float itemCenter = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
    EXPECT_NEAR(itemCenter, (barMinY + barMaxY) * 0.5f, 0.5f);

    ImGui::EndMainMenuBar();
}

TEST_F(ChromeBarsTests, SecondMenuBarAppendSeatsChromeOnTheLane)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(style.FramePadding.x, style.FramePadding.y + EditorChrome::BarLaneInset()));
    const bool open = ImGui::BeginMainMenuBar();
    ImGui::PopStyleVar();
    ASSERT_TRUE(open);

    const ImVec2 barMin = ImGui::GetWindowPos();
    const ImVec2 barMax(barMin.x + ImGui::GetWindowSize().x, barMin.y + ImGui::GetWindowSize().y);
    const float buttonSize = EditorChrome::BarButtonSize();
    // The bar is exactly the height the chassis asks for, so its lane is the
    // one the chrome is seated on.
    EXPECT_FLOAT_EQ(barMax.y - barMin.y, EditorChrome::BarHeight(buttonSize));

    if (ImGui::BeginMenu("File"))
        ImGui::EndMenu();

    // The second append takes the ordinary padding back, which is what lets a
    // framed control sit on the lane instead of on the menus' text baseline.
    ImGui::EndMenuBar();
    ASSERT_TRUE(ImGui::BeginMenuBar());

    const EditorChrome::BarRects bar =
        EditorChrome::BarFrame(ImGui::GetWindowDrawList(), barMin, barMax, EditorChrome::BarEdge::Bottom, buttonSize,
                               EditorChrome::BarSurface{});
    ImGui::SetCursorScreenPos(ImVec2(barMin.x + 200.0f, bar.LaneMin.y));
    ImGui::Button("ok", ImVec2(buttonSize, buttonSize));

    EXPECT_FLOAT_EQ(ImGui::GetItemRectMin().y, bar.LaneMin.y);
    EXPECT_FLOAT_EQ(ImGui::GetItemRectMax().y, bar.LaneMax.y);
    // And the lane is centered in the bar, so the control shares the menus'
    // centerline rather than hanging below it.
    EXPECT_NEAR((bar.LaneMin.y + bar.LaneMax.y) * 0.5f, (barMin.y + barMax.y) * 0.5f, 0.5f);

    ImGui::EndMainMenuBar();
}
