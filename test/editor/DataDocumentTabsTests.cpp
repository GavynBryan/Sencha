// Clicking another document's tab while typing commits the typed edit once,
// with one notification and one resident push, and a document the set brings
// forward stays forward -- driven through real ImGui input, frame by frame.

#include "data/DataDocumentSet.h"
#include "ui/DataDocumentTabs.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonFormat.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

namespace
{
    constexpr std::string_view kTyped = "asset://typed.sdata";
    constexpr std::string_view kOther = "asset://other.sdata";
    constexpr std::string_view kLabelType = "test.label";

    void RegisterLabelType(RuntimeAssets& assets)
    {
        const bool registered = assets.DataTypes.Register({ std::string(kLabelType), 1, [](const JsonValue& data) {
            DataAssetCompileResult result;
            result.Value = std::make_shared<std::string>(data.Find("label")->AsString());
            return result;
        } });
        DataFieldSchema label;
        label.Key = "label";
        label.Kind = DataFieldKind::String;
        DataSchema schema;
        schema.TypeName = std::string(kLabelType);
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children = { std::move(label) };
        ASSERT_TRUE(registered && assets.DataSchemas.Register(std::move(schema)));
    }

    struct HeadlessTabs : testing::Test
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path()
            / ("sencha_data_document_tabs_" + std::to_string(std::random_device{}()));
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };
        DocumentSourceSet Sources;
        std::unique_ptr<DataDocumentSet> Set;
        std::unique_ptr<DataDocumentTabs> Tabs;
        int Notified = 0;

        void SetUp() override
        {
            RegisterLabelType(Assets);
            std::filesystem::create_directories(Root);
            for (const auto& [name, label] : { std::pair{ "typed.sdata", "Before" }, std::pair{ "other.sdata", "Other" } })
                std::ofstream(Root / name) << R"({ "type": "test.label", "version": 1, "data": { "label": ")" << label
                                           << R"(" } })";
            (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
            Set = std::make_unique<DataDocumentSet>(Assets, Sources, DataDocumentSetConfig{ .ContentRoot = Root, .Subtypes = {} });
            Set->OnChanged([this](DataDocument&, bool) { ++Notified; });
            Tabs = std::make_unique<DataDocumentTabs>(*Set);

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(1024, 768);
            io.DeltaTime = 1.0f / 60.0f;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.Fonts->AddFontDefault();
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }

        void TearDown() override
        {
            ImGui::DestroyContext();
            Tabs.reset();
            Set.reset();
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        void Frame()
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(1000, 740));
            ImGui::Begin("Documents");
            Tabs->Draw();
            ImGui::End();
            ImGui::Render();
        }

        void Frames(int count)
        {
            for (int i = 0; i < count; ++i)
                Frame();
        }

        void Press(ImGuiKey key)
        {
            ImGui::GetIO().AddKeyEvent(key, true);
            Frame();
            ImGui::GetIO().AddKeyEvent(key, false);
            Frame();
        }

        void Click(ImVec2 at)
        {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(at.x, at.y);
            Frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            Frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            Frame();
        }

        [[nodiscard]] static ImVec2 TabCenter(std::size_t index)
        {
            ImGuiTabBar* bar = GImGui->TabBars.GetByIndex(0);
            const ImGuiTabItem& tab = bar->Tabs[static_cast<int>(index)];
            return { bar->BarRect.Min.x + tab.Offset + tab.Width * 0.5f, (bar->BarRect.Min.y + bar->BarRect.Max.y) * 0.5f };
        }

        [[nodiscard]] static std::string LabelOf(const DataDocument& document)
        {
            return document.Data()->Find("label")->AsString();
        }

        [[nodiscard]] std::string ResidentLabel() const
        {
            const auto* label = static_cast<const std::string*>(Assets.DataAssets.GetRaw(Assets.DataAssets.Find(kTyped)));
            return label != nullptr ? *label : std::string();
        }
    };
}

TEST_F(HeadlessTabs, ClickingAnotherTabCommitsTheTypedEditOnce)
{
    std::string error;
    DataDocument& other = *Set->OpenOrFocus(kOther, error);
    DataDocument& typed = *Set->OpenOrFocus(kTyped, error);
    AssetLease lease = Assets.Assets.LoadLease(kTyped, AssetType::Data);
    ASSERT_TRUE(lease);
    Frames(3);

    // Tabbing onto the text field activates it; the loop stops there.
    for (int attempt = 0; attempt < 12 && GImGui->ActiveId == 0; ++attempt)
        Press(ImGuiKey_Tab);
    ASSERT_NE(GImGui->ActiveId, 0u) << "no text field took focus";
    ImGui::GetIO().AddInputCharactersUTF8("After");
    Frame();
    ASSERT_TRUE(typed.IsEditing()) << "typing opened an edit";
    ASSERT_EQ(LabelOf(typed), "After") << JsonFormat(typed.Root());
    ASSERT_EQ(ResidentLabel(), "Before") << "a preview never reaches the resident asset";
    const uint64_t reloads = Assets.DataAssets.GetReloadVersion(Assets.DataAssets.Find(kTyped));
    Notified = 0;

    Click(TabCenter(0));
    Frames(2);
    EXPECT_EQ(Set->Active(), &other);
    EXPECT_FALSE(typed.IsEditing());
    EXPECT_EQ(LabelOf(typed), "After") << "the typed text is kept";
    EXPECT_EQ(Notified, 1);
    EXPECT_EQ(Assets.DataAssets.GetReloadVersion(Assets.DataAssets.Find(kTyped)), reloads + 1);
    EXPECT_EQ(ResidentLabel(), "After");

    Sources.Undo();
    EXPECT_EQ(LabelOf(typed), "Before") << "the typed edit is one step";
    EXPECT_FALSE(Sources.CanUndo());
}

TEST_F(HeadlessTabs, OpeningADocumentShowsItsTabAndStaysThere)
{
    std::string error;
    (void)Set->OpenOrFocus(kOther, error);
    Frames(3);
    DataDocument& opened = *Set->OpenOrFocus(kTyped, error);
    Frames(4);
    EXPECT_EQ(Set->Active(), &opened);
    EXPECT_EQ(GImGui->TabBars.GetByIndex(0)->SelectedTabId, GImGui->TabBars.GetByIndex(0)->Tabs[1].ID);
}
