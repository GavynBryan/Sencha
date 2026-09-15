#include <gtest/gtest.h>

#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageSerializer.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <SDL3/SDL.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

// A stylesheet the host supplies under a name the package already references.
//
// This is how an application's theme reaches an authored surface without the
// document carrying the theme, and without theme values travelling through a
// presentation model. The package is cooked with its own copy so it opens in a
// process that supplies nothing; the host replaces those bytes and the open
// documents restyle.
//
// The part worth protecting is that it restyles rather than rebuilds. A theme
// change that reconstructed the document would move the caret, drop the focus,
// and reset the scroll of anybody who happened to be working at the time.

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
             / ("sencha_ui_theme_test_" + std::to_string(rd()));
        std::filesystem::create_directories(Root);
    }
    ~TempAssetRoot()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
    TempAssetRoot(const TempAssetRoot&) = delete;
    TempAssetRoot& operator=(const TempAssetRoot&) = delete;

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

// Structure in the document's own sheet, appearance in the theme's -- the
// separation the whole mechanism exists to keep. The panel's height comes from
// the theme here only so a test can measure what a colour change cannot show.
constexpr std::string_view kMarkup = R"(<rml>
<head>
    <link type="text/rcss" href="panel.rcss"/>
    <link type="text/rcss" href="theme.rcss"/>
</head>
<body data-model="skin">
    <div id="panel"><input id="field" type="text" data-value="text"/></div>
</body>
</rml>)";

constexpr std::string_view kStructure = R"(
body { display: block; width: 100%; height: 100%; pointer-events: none; }
#panel { display: block; position: absolute; left: 0; top: 0; width: 200px;
         pointer-events: auto; }
#field { display: block; width: 180px; height: 20px; pointer-events: auto;
         tab-index: auto; }
)";

// What a package carries so that it opens in a process supplying nothing.
constexpr std::string_view kPackagedTheme = "#panel { height: 40px; }";

UiPackage MakePackage()
{
    UiPackage package;
    package.RootDocumentName = "panel.rml";

    const auto blob = [&](std::string_view name, UiBlobKind kind, std::string_view text) {
        UiPackageBlob out;
        out.VirtualName = std::string(name);
        out.SourcePath = "ui/" + std::string(name);
        out.Kind = kind;
        out.Bytes = BytesOf(text);
        package.Blobs.push_back(std::move(out));
    };
    blob("panel.rml", UiBlobKind::Document, kMarkup);
    blob("panel.rcss", UiBlobKind::StyleSheet, kStructure);
    blob("theme.rcss", UiBlobKind::StyleSheet, kPackagedTheme);
    return package;
}

UiScreenDesc MakeDesc()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://ui/panel.sui";
    desc.ModelName = "skin";
    desc.Properties = { UiModelProperty{ "text", UiValue(std::string("abc")), true } };
    return desc;
}

class Fixture
{
public:
    Fixture()
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        std::vector<std::byte> bytes;
        EXPECT_TRUE(WriteSuiToBytes(MakePackage(), bytes));
        Root.WriteBytes("ui/panel.sui", bytes);
        ScanAssetsDirectory(Root.PathString(), Assets.Registry, Assets.Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages,
                                         Assets.Fonts, nullptr, nullptr);
        Surface = Ui->CreateSurface("test", RenderExtent{ 800, 600 });
        Screen = Ui->OpenScreen(Surface, MakeDesc());
        Ui->Update();
    }
    ~Fixture() { if (Ui != nullptr) Ui->Shutdown(); }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    [[nodiscard]] float PanelHeight() const
    {
        const auto box = Ui->MeasureElement(Screen, "panel");
        return box.has_value() ? box->Height : -1.0f;
    }

    UiService& Service() { return *Ui; }

    TempAssetRoot Root;
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
    UiSurfaceId Surface;
    UiScreenHandle Screen;
};
} // namespace

TEST(UiHostStyle, APackageUsesItsOwnCopyWhenTheHostSuppliesNothing)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());
    EXPECT_FLOAT_EQ(fixture.PanelHeight(), 40.0f)
        << "the package's own stylesheet did not apply";
}

TEST(UiHostStyle, AHostSheetReplacesThePackagesCopy)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    EXPECT_TRUE(fixture.Service().SetHostStyleSheet("theme.rcss", "#panel { height: 90px; }"));
    fixture.Service().Update();
    EXPECT_FLOAT_EQ(fixture.PanelHeight(), 90.0f)
        << "the open document kept the sheet the package carried";
}

TEST(UiHostStyle, ReplacingItWithTheSameTextRestylesNothing)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    EXPECT_TRUE(fixture.Service().SetHostStyleSheet("theme.rcss", "#panel { height: 90px; }"));
    EXPECT_FALSE(fixture.Service().SetHostStyleSheet("theme.rcss", "#panel { height: 90px; }"))
        << "a host handing over the same theme every frame would restyle every frame";
}

TEST(UiHostStyle, ClearingItPutsThePackageBackOnItsOwnCopy)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    EXPECT_TRUE(fixture.Service().SetHostStyleSheet("theme.rcss", "#panel { height: 90px; }"));
    fixture.Service().Update();
    ASSERT_FLOAT_EQ(fixture.PanelHeight(), 90.0f);

    EXPECT_TRUE(fixture.Service().SetHostStyleSheet("theme.rcss", ""));
    fixture.Service().Update();
    EXPECT_FLOAT_EQ(fixture.PanelHeight(), 40.0f);
}

TEST(UiHostStyle, AScreenOpenedAfterTheHostSheetUsesIt)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    EXPECT_TRUE(fixture.Service().SetHostStyleSheet("theme.rcss", "#panel { height: 70px; }"));
    fixture.Service().CloseScreen(fixture.Screen);
    fixture.Screen = fixture.Service().OpenScreen(fixture.Surface, MakeDesc());
    fixture.Service().Update();

    ASSERT_TRUE(fixture.Screen.IsValid());
    EXPECT_FLOAT_EQ(fixture.PanelHeight(), 70.0f);
}

TEST(UiHostStyle, ARestyleKeepsFocusAndWhatWasBeingTyped)
{
    // The reason this restyles instead of rebuilding. Somebody mid-edit when a
    // theme changes must not lose the field they were in or the text in it.
    Fixture fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());

    SDL_Event move{};
    move.type = SDL_EVENT_MOUSE_MOTION;
    move.motion.x = 90.0f;
    move.motion.y = 10.0f;
    (void)fixture.Service().ProcessPlatformEvent(move);
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.x = 90.0f;
    down.button.y = 10.0f;
    (void)fixture.Service().ProcessPlatformEvent(down);
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    (void)fixture.Service().ProcessPlatformEvent(up);
    fixture.Service().Update();

    SDL_Event typed{};
    typed.type = SDL_EVENT_TEXT_INPUT;
    typed.text.text = "z";
    (void)fixture.Service().ProcessPlatformEvent(typed);
    fixture.Service().Update();

    const std::string mid(
        fixture.Service().GetValue(fixture.Screen, UiPropertyIdAt(0)).AsString());
    ASSERT_NE(mid, "abc") << "the click never reached the field";
    ASSERT_TRUE(fixture.Service().Capture().Keyboard);

    EXPECT_TRUE(fixture.Service().SetHostStyleSheet("theme.rcss", "#panel { height: 90px; }"));
    fixture.Service().Update();

    EXPECT_FLOAT_EQ(fixture.PanelHeight(), 90.0f) << "the restyle did not happen";
    EXPECT_EQ(fixture.Service().GetValue(fixture.Screen, UiPropertyIdAt(0)).AsString(), mid)
        << "the theme change threw away what was being typed";
    EXPECT_TRUE(fixture.Service().Capture().Keyboard)
        << "the theme change took the focus out of the field somebody was in";
}
