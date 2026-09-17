#include <gtest/gtest.h>

#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageSerializer.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

//=============================================================================
// Looking at a laid-out document from outside, without the DOM.
//
// The contract under test is the reference: a ticket that answers for its
// element for exactly as long as the element exists, and for nothing after --
// not for whatever a model-driven update or a rebuild put in its place.
//=============================================================================

namespace
{
std::vector<std::byte> BytesOf(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

class TempAssetRoot
{
public:
    TempAssetRoot()
    {
        std::random_device rd;
        Root = std::filesystem::temp_directory_path()
             / ("sencha_ui_inspect_test_" + std::to_string(rd()));
        std::filesystem::create_directories(Root);
    }
    ~TempAssetRoot()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
    void WriteBytes(std::string_view relPath, std::span<const std::byte> bytes) const
    {
        const std::filesystem::path full = Root / relPath;
        std::filesystem::create_directories(full.parent_path());
        std::ofstream file(full, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    [[nodiscard]] std::string PathString() const { return Root.generic_string(); }

private:
    std::filesystem::path Root;
};

constexpr std::string_view kMarkup = R"(<rml>
<head><link type="text/rcss" href="doc.rcss"/></head>
<body data-model="m">
    <div id="panel" class="frame primary">
        <div id="button" data-event-click="press"/>
        <div id="list"><div class="row" data-for="row : rows">{{row.label}}</div></div>
    </div>
</body>
</rml>)";

constexpr std::string_view kStyle = R"(
body { display: block; width: 100%; height: 100%; pointer-events: none; }
#panel { display: block; position: absolute; left: 10px; top: 10px; width: 300px; height: 200px;
         padding: 5px; border-width: 2px; margin: 3px; }
#button { display: block; width: 100px; height: 30px; pointer-events: auto; }
#list { display: block; }
.row { display: block; height: 20px; }
)";

UiPackage MakePackage()
{
    UiPackage package;
    package.RootDocumentName = "doc.rml";
    UiPackageBlob root;
    root.VirtualName = "doc.rml";
    root.SourcePath = "ui/doc.rml";
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(kMarkup);
    package.Blobs.push_back(std::move(root));
    UiPackageBlob sheet;
    sheet.VirtualName = "doc.rcss";
    sheet.SourcePath = "ui/doc.rcss";
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(kStyle);
    package.Blobs.push_back(std::move(sheet));
    return package;
}

struct Fixture
{
    TempAssetRoot Root;
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    std::unique_ptr<RuntimeAssets> Assets;
    std::unique_ptr<UiService> Ui;
    UiSurfaceId Surface;
    UiScreenHandle Screen;

    Fixture()
    {
        std::vector<std::byte> bytes;
        EXPECT_TRUE(WriteSuiToBytes(MakePackage(), bytes));
        Root.WriteBytes("ui/doc.sui", bytes);
        Assets = std::make_unique<RuntimeAssets>(Logging, Serializers, RuntimeAssets::ReferenceOnly{});
        ScanAssetsDirectory(Root.PathString(), Assets->Registry, Assets->Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets->Assets, Assets->UiPackages,
                                         Assets->Fonts, nullptr, nullptr);
        Surface = Ui->CreateSurface("test", RenderExtent{ 800, 600 });
        UiScreenDesc desc;
        desc.PackagePath = "asset://ui/doc.sui";
        desc.ModelName = "m";
        desc.RowLists = { "rows" };
        desc.Actions = { "press" };
        Screen = Ui->OpenScreen(Surface, desc);
        Ui->Update();
    }
    ~Fixture() { Ui->Shutdown(); }

    void PublishRows(std::size_t count)
    {
        std::vector<UiRow> rows;
        for (std::size_t i = 0; i < count; ++i)
            rows.push_back(UiRow{ "r" + std::to_string(i), "", "" });
        (void)Ui->SetRows(Screen, UiRowsIdAt(0), rows);
        Ui->Update();
    }

    [[nodiscard]] UiElementRef Find(std::string_view id)
    {
        for (const UiElementInfo& info : Ui->ElementTree(Screen))
            if (info.Id == id)
                return info.Ref;
        return {};
    }
};
} // namespace

TEST(UiInspection, TheElementUnderAPointIsTheInnermostOne)
{
    Fixture f;
    ASSERT_TRUE(f.Screen.IsValid());
    // #button sits at the panel's content origin: 10 + 3 margin + 2 border + 5 padding.
    const UiElementRef ref = f.Ui->ElementAt(f.Surface, Vec2d{ 30.0f, 30.0f });
    ASSERT_TRUE(ref.IsValid());
    const auto info = f.Ui->DescribeElement(ref);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->Id, "button");
    EXPECT_EQ(info->Tag, "div");
    EXPECT_EQ(ref.Screen, f.Screen);

    EXPECT_FALSE(f.Ui->ElementAt(f.Surface, Vec2d{ 700.0f, 500.0f }).IsValid())
        << "nothing authored is there, and the body does not take the pointer";
}

TEST(UiInspection, DescriptionCarriesTheBoxModelTagIdClassesAndAttributes)
{
    Fixture f;
    const UiElementRef panel = f.Find("panel");
    ASSERT_TRUE(panel.IsValid());
    const auto info = f.Ui->DescribeElement(panel);
    ASSERT_TRUE(info.has_value());

    EXPECT_EQ(info->Classes, (std::vector<std::string>{ "frame", "primary" }));
    const bool hasId = std::any_of(info->Attributes.begin(), info->Attributes.end(),
                                   [](const auto& kv) { return kv.first == "id" && kv.second == "panel"; });
    EXPECT_TRUE(hasId);

    // Content agrees with the measurement the substrate already offered, and
    // the boxes nest the way the box model says they do.
    const auto measured = f.Ui->MeasureElement(f.Screen, "panel");
    ASSERT_TRUE(measured.has_value());
    EXPECT_FLOAT_EQ(info->Boxes.Content.X, measured->X);
    EXPECT_FLOAT_EQ(info->Boxes.Content.Width, measured->Width);
    EXPECT_FLOAT_EQ(info->Boxes.Padding.Width, info->Boxes.Content.Width + 10.0f);
    EXPECT_FLOAT_EQ(info->Boxes.Border.Width, info->Boxes.Padding.Width + 4.0f);
    EXPECT_FLOAT_EQ(info->Boxes.Margin.Width, info->Boxes.Border.Width + 6.0f);
    EXPECT_LT(info->Boxes.Margin.X, info->Boxes.Border.X);
    EXPECT_LT(info->Boxes.Border.X, info->Boxes.Padding.X);
    EXPECT_LT(info->Boxes.Padding.X, info->Boxes.Content.X);
}

TEST(UiInspection, TheTreeIsPreorderWithConsistentParents)
{
    Fixture f;
    f.PublishRows(2);
    const std::vector<UiElementInfo> tree = f.Ui->ElementTree(f.Screen);
    ASSERT_GE(tree.size(), 6u);
    EXPECT_EQ(tree.front().Depth, 0u);
    EXPECT_FALSE(tree.front().Parent.IsValid()) << "the root has no parent a screen owns";

    // Every non-root entry's parent appears earlier in the list, one level up.
    for (std::size_t i = 1; i < tree.size(); ++i)
    {
        const auto parent = std::find_if(tree.begin(), tree.begin() + static_cast<long>(i),
                                         [&](const UiElementInfo& e) { return e.Ref == tree[i].Parent; });
        ASSERT_NE(parent, tree.begin() + static_cast<long>(i)) << "parent of #" << i << " not before it";
        EXPECT_EQ(parent->Depth + 1, tree[i].Depth);
    }
    // The children query agrees with the tree.
    const UiElementRef panel = f.Find("panel");
    const std::vector<UiElementRef> children = f.Ui->ElementChildren(panel);
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(f.Ui->DescribeElement(children[0])->Id, "button");
    EXPECT_EQ(f.Ui->DescribeElement(children[1])->Id, "list");
}

TEST(UiInspection, ARefSurvivesUpdatesItsElementSurvivesAndDiesWithIt)
{
    Fixture f;
    f.PublishRows(3);
    // The list holds the generated rows and, after them, the hidden template
    // the repeat was authored as. The template is the one that never goes
    // anywhere; the generated rows are the ones a shrinking list removes.
    std::vector<UiElementRef> generated;
    for (const UiElementRef& child : f.Ui->ElementChildren(f.Find("list")))
        if (f.Ui->ComputedProperty(child, "display").value_or("") != "none")
            generated.push_back(child);
    ASSERT_EQ(generated.size(), 3u);
    const UiElementRef firstRow = generated.front();
    const UiElementRef lastRow = generated.back();
    ASSERT_TRUE(f.Ui->DescribeElement(lastRow).has_value());

    // An update that touches something else leaves both standing.
    f.PublishRows(3);
    EXPECT_TRUE(f.Ui->DescribeElement(firstRow).has_value());
    EXPECT_TRUE(f.Ui->DescribeElement(lastRow).has_value());

    // An update that removes the last row kills its ref and nobody else's.
    f.PublishRows(2);
    EXPECT_TRUE(f.Ui->DescribeElement(firstRow).has_value());
    EXPECT_FALSE(f.Ui->DescribeElement(lastRow).has_value())
        << "a ref answered for an element that no longer exists";
    EXPECT_TRUE(f.Ui->ElementChildren(lastRow).empty());
    EXPECT_FALSE(f.Ui->ComputedProperty(lastRow, "display").has_value());
}

TEST(UiInspection, EveryRefDiesWithARebuildAndWithTheScreen)
{
    Fixture f;
    const UiElementRef panel = f.Find("panel");
    ASSERT_TRUE(f.Ui->DescribeElement(panel).has_value());

    // The same package landing again is what an edit does: the document is
    // rebuilt, the handle survives, the elements do not.
    ASSERT_TRUE(f.Assets->UiPackages.ReloadInPlace("asset://ui/doc.sui", MakePackage()));
    f.Ui->Update();
    EXPECT_TRUE(f.Ui->IsScreenOpen(f.Screen));
    EXPECT_FALSE(f.Ui->DescribeElement(panel).has_value());
    // And a fresh query answers again.
    const UiElementRef again = f.Find("panel");
    ASSERT_TRUE(again.IsValid());
    EXPECT_NE(again, panel) << "a rebuilt element is a new ticket";

    f.Ui->CloseScreen(f.Screen);
    EXPECT_FALSE(f.Ui->DescribeElement(again).has_value());
    EXPECT_TRUE(f.Ui->ElementTree(f.Screen).empty());
}

TEST(UiInspection, AComputedPropertyReadsAsTheEngineWouldPrintIt)
{
    Fixture f;
    const UiElementRef button = f.Find("button");
    ASSERT_TRUE(button.IsValid());
    EXPECT_EQ(f.Ui->ComputedProperty(button, "display").value_or(""), "block");
    EXPECT_EQ(f.Ui->ComputedProperty(button, "pointer-events").value_or(""), "auto");
    EXPECT_FALSE(f.Ui->ComputedProperty(button, "not-a-property").has_value());
}
