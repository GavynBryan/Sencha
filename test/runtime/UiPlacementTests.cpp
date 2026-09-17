#include <gtest/gtest.h>

#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageSerializer.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <ui/UiSurfacePlacement.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <SDL3/SDL.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

//=============================================================================
// A surface shown somewhere other than the whole window.
//
// Placement is a coordinate contract -- window points in, surface pixels out,
// density never consulted -- and a dispatch rule: what is inside reaches the
// surface, what is outside does not, and a press that landed inside keeps the
// pointer until it is released. Input policy is what a host says a surface may
// receive at all.
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
             / ("sencha_ui_place_test_" + std::to_string(rd()));
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

// A 1920x1080 document: a button at the top left, a slider below it, and a
// text field, each large enough to hit at a coarse fit.
constexpr std::string_view kMarkup = R"rml(<rml>
<head><link type="text/rcss" href="doc.rcss"/></head>
<body data-model="m">
    <div id="button" data-event-click="pressed"/>
    <input id="slider" type="range" min="0" max="100" step="1" data-value="level"
           data-event-change="changed(ev.value)"/>
    <input id="field" type="text" data-value="name"/>
</body>
</rml>)rml";

constexpr std::string_view kStyle = R"(
body { display: block; width: 100%; height: 100%; pointer-events: none; }
#button { display: block; position: absolute; left: 100px; top: 100px; width: 400px; height: 200px; pointer-events: auto; }
#slider { display: block; position: absolute; left: 100px; top: 400px; width: 1000px; height: 60px; pointer-events: auto; tab-index: auto; }
slidertrack { display: block; height: 60px; pointer-events: auto; }
sliderbar { display: block; width: 40px; height: 60px; pointer-events: auto; }
sliderprogress { display: block; height: 60px; pointer-events: auto; }
sliderarrowdec, sliderarrowinc { display: none; }
#field { display: block; position: absolute; left: 100px; top: 600px; width: 600px; height: 80px; pointer-events: auto; tab-index: auto; }
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

constexpr auto kPressed = UiActionId{ 1 };
constexpr auto kChanged = UiActionId{ 2 };
constexpr auto kLevel = UiModelPropertyId{ 1 };
constexpr auto kName = UiModelPropertyId{ 2 };

struct Fixture
{
    TempAssetRoot Root;
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    std::unique_ptr<RuntimeAssets> Assets;
    std::unique_ptr<UiService> Ui;
    UiSurfaceId Surface;
    UiScreenHandle Screen;
    // The surface is 1920x1080 and is shown fitted into an 800x450 rect at (100, 50):
    // 2.4 surface pixels per window point.
    const Rect2d Placed{ 100.0f, 50.0f, 800.0f, 450.0f };
    const RenderExtent Size{ 1920, 1080 };

    Fixture()
    {
        std::vector<std::byte> bytes;
        EXPECT_TRUE(WriteSuiToBytes(MakePackage(), bytes));
        Root.WriteBytes("ui/doc.sui", bytes);
        Assets = std::make_unique<RuntimeAssets>(Logging, Serializers, RuntimeAssets::ReferenceOnly{});
        ScanAssetsDirectory(Root.PathString(), Assets->Registry, Assets->Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets->Assets, Assets->UiPackages,
                                         Assets->Fonts, nullptr, nullptr);
        Surface = Ui->CreateSurface("preview", Size);
        UiScreenDesc desc;
        desc.PackagePath = "asset://ui/doc.sui";
        desc.ModelName = "m";
        desc.Properties = {
            UiModelProperty{ "level", UiValue(50.0), true },
            UiModelProperty{ "name", UiValue(std::string("")), true },
        };
        desc.Actions = { "pressed", "changed" };
        Screen = Ui->OpenScreen(Surface, desc);
        Ui->Update();
        (void)Ui->DrainActions();
    }
    ~Fixture() { Ui->Shutdown(); }

    // Window point for a surface pixel under the placement.
    [[nodiscard]] Vec2d WindowFor(float sx, float sy) const
    {
        return MapSurfacePointToWindow(Vec2d{ sx, sy }, Placed, Size);
    }

    void Move(Vec2d w)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.x = w.X;
        e.motion.y = w.Y;
        LastConsumed = Ui->ProcessPlatformEvent(e);
        Ui->Update();
    }
    void Button(Vec2d w, bool down)
    {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.x = w.X;
        e.button.y = w.Y;
        LastConsumed = Ui->ProcessPlatformEvent(e);
        Ui->Update();
    }
    void Click(Vec2d w)
    {
        Move(w);
        Button(w, true);
        Button(w, false);
    }
    void Type(const char* text)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_TEXT_INPUT;
        e.text.text = text;
        LastConsumed = Ui->ProcessPlatformEvent(e);
        Ui->Update();
    }
    bool LastConsumed = false;

    [[nodiscard]] bool Raised(UiActionId id)
    {
        for (const UiAction& action : Ui->DrainActions(Screen))
            if (action.Id == id)
                return true;
        return false;
    }
};
} // namespace

// -- the pure mapping ----------------------------------------------------------

TEST(UiSurfacePlacement, MapsByRatioAndNeverByDensity)
{
    const Rect2d rect{ 100.0f, 50.0f, 800.0f, 450.0f };
    const RenderExtent surface{ 1920, 1080 };

    struct Case { Vec2d Window; std::optional<Vec2d> Expect; };
    const Case cases[] = {
        { { 100.0f, 50.0f }, Vec2d{ 0.0f, 0.0f } },              // top-left corner is inside
        { { 500.0f, 275.0f }, Vec2d{ 960.0f, 540.0f } },         // centre maps to centre
        { { 899.0f, 499.0f }, Vec2d{ 1917.6f, 1077.6f } },       // last point inside
        { { 900.0f, 500.0f }, std::nullopt },                    // far edge is outside (half-open)
        { { 99.0f, 60.0f }, std::nullopt },                      // just left
        { { 0.0f, 0.0f }, std::nullopt },
    };
    for (const Case& c : cases)
    {
        const auto mapped = MapWindowPointToSurface(c.Window, rect, surface);
        ASSERT_EQ(mapped.has_value(), c.Expect.has_value()) << c.Window.X << "," << c.Window.Y;
        if (mapped)
        {
            EXPECT_NEAR(mapped->X, c.Expect->X, 0.01f);
            EXPECT_NEAR(mapped->Y, c.Expect->Y, 0.01f);
        }
    }

    // A 400-point rect over an 800-pixel surface maps two pixels per point --
    // and it does so identically whether the display is 1x or 2x, because the
    // density is not an input. It enters only through which surface size the
    // host chose.
    const Rect2d small{ 0.0f, 0.0f, 400.0f, 225.0f };
    const auto hi = MapWindowPointToSurface(Vec2d{ 200.0f, 100.0f }, small, RenderExtent{ 800, 450 });
    ASSERT_TRUE(hi.has_value());
    EXPECT_FLOAT_EQ(hi->X, 400.0f);
    EXPECT_FLOAT_EQ(hi->Y, 200.0f);

    // Non-uniform fit maps each axis by its own ratio.
    const auto skew = MapWindowPointToSurface(Vec2d{ 50.0f, 50.0f }, Rect2d{ 0.0f, 0.0f, 100.0f, 200.0f },
                                              RenderExtent{ 1000, 1000 });
    ASSERT_TRUE(skew.has_value());
    EXPECT_FLOAT_EQ(skew->X, 500.0f);
    EXPECT_FLOAT_EQ(skew->Y, 250.0f);

    // The inverse round-trips, and extrapolates off the surface.
    const Vec2d back = MapSurfacePointToWindow(Vec2d{ 960.0f, 540.0f }, rect, surface);
    EXPECT_FLOAT_EQ(back.X, 500.0f);
    EXPECT_FLOAT_EQ(back.Y, 275.0f);
    EXPECT_LT(MapSurfacePointToWindow(Vec2d{ -100.0f, 0.0f }, rect, surface).X, rect.Position.X);
}

// -- dispatch through a placement ---------------------------------------------

TEST(UiSurfacePlacement, AClickInsideThePlacementHitsTheMappedElement)
{
    Fixture f;
    ASSERT_TRUE(f.Screen.IsValid());
    f.Ui->SetSurfacePlacement(f.Surface, f.Placed);

    // The button covers surface (100..500, 100..300); its centre is (300, 200).
    f.Click(f.WindowFor(300.0f, 200.0f));
    EXPECT_TRUE(f.LastConsumed);
    EXPECT_TRUE(f.Raised(kPressed)) << "the press did not land on the button the point maps to";
}

TEST(UiSurfacePlacement, OutsideThePlacementNothingReachesTheSurfaceAndNothingIsCaptured)
{
    Fixture f;
    f.Ui->SetSurfacePlacement(f.Surface, f.Placed);

    // Window (300, 200) would be the button at 1:1 -- but the placement starts
    // at (100, 50) and this point maps to surface (480, 360), between the
    // button and the slider. And window (50, 25) is outside the rect entirely.
    f.Click(Vec2d{ 50.0f, 25.0f });
    EXPECT_FALSE(f.LastConsumed);
    EXPECT_FALSE(f.Raised(kPressed));
    EXPECT_FALSE(f.Ui->Capture().Mouse);

    // Hover the button, then leave the rect: capture follows the pointer out,
    // and so does the over-ness a host reads to hand the keyboard around.
    f.Move(f.WindowFor(300.0f, 200.0f));
    EXPECT_TRUE(f.Ui->Capture().Mouse);
    EXPECT_TRUE(f.Ui->IsPointerOver(f.Surface));
    f.Move(Vec2d{ 50.0f, 25.0f });
    EXPECT_FALSE(f.Ui->Capture().Mouse) << "a surface the pointer left kept claiming it";
    EXPECT_FALSE(f.Ui->IsPointerOver(f.Surface));
}

TEST(UiSurfacePlacement, ADragThatStartsInsideKeepsThePointerUntilItIsReleased)
{
    Fixture f;
    f.Ui->SetSurfacePlacement(f.Surface, f.Placed);
    (void)f.Ui->DrainActions();

    // Press on the slider's bar (value 50 of 100 sits at the track's middle),
    // drag right past the placement's edge, release out there.
    const Vec2d grab = f.WindowFor(600.0f, 430.0f);
    f.Move(grab);
    f.Button(grab, true);
    EXPECT_TRUE(f.LastConsumed);
    f.Move(Vec2d{ f.Placed.Position.X + f.Placed.Size.X + 5.0f, grab.Y });
    EXPECT_TRUE(f.LastConsumed) << "the drag was dropped at the boundary";
    EXPECT_TRUE(f.Ui->Capture().Mouse) << "the surface let go of a held pointer";
    f.Button(Vec2d{ f.Placed.Position.X + f.Placed.Size.X + 5.0f, grab.Y }, false);
    EXPECT_TRUE(f.LastConsumed);

    EXPECT_GT(f.Ui->GetValue(f.Screen, kLevel).AsFloat(), 50.0)
        << "the slider did not follow a drag that left the placement";
    EXPECT_TRUE(f.Raised(kChanged));
    EXPECT_FALSE(f.Ui->Capture().Mouse) << "released, and outside: nothing to claim";
}

TEST(UiSurfacePlacement, TwoPlacedSurfacesDoNotReceiveEachOthersClicks)
{
    Fixture f;
    // A second surface, same document, shown to the right of the first.
    const UiSurfaceId other = f.Ui->CreateSurface("other", f.Size);
    UiScreenDesc desc;
    desc.PackagePath = "asset://ui/doc.sui";
    desc.ModelName = "m";
    desc.Properties = { UiModelProperty{ "level", UiValue(50.0), true },
                        UiModelProperty{ "name", UiValue(std::string("")), true } };
    desc.Actions = { "pressed", "changed" };
    const UiScreenHandle otherScreen = f.Ui->OpenScreen(other, desc);
    ASSERT_TRUE(otherScreen.IsValid());
    f.Ui->Update();
    (void)f.Ui->DrainActions();

    f.Ui->SetSurfacePlacement(f.Surface, f.Placed);
    const Rect2d right{ 1000.0f, 50.0f, 800.0f, 450.0f };
    f.Ui->SetSurfacePlacement(other, right);

    f.Click(MapSurfacePointToWindow(Vec2d{ 300.0f, 200.0f }, right, f.Size));
    bool firstPressed = false;
    for (const UiAction& a : f.Ui->DrainActions(f.Screen))
        firstPressed |= a.Id == kPressed;
    bool otherPressed = false;
    for (const UiAction& a : f.Ui->DrainActions(otherScreen))
        otherPressed |= a.Id == kPressed;
    EXPECT_FALSE(firstPressed) << "a click over the second surface reached the first";
    EXPECT_TRUE(otherPressed);
}

TEST(UiSurfacePlacement, NoPlacementIsTheWholeWindowAtOneToOne)
{
    // Every existing host: no placement, and window points are surface pixels.
    Fixture f;
    f.Click(Vec2d{ 300.0f, 200.0f });
    EXPECT_TRUE(f.Raised(kPressed));
    EXPECT_FALSE(f.Ui->GetSurfacePlacement(f.Surface).has_value());
}

// -- input policy --------------------------------------------------------------

TEST(UiSurfaceInputPolicy, DisabledDeliversNothingWhileInspectionStillAnswers)
{
    Fixture f;
    f.Ui->SetSurfaceInputPolicy(f.Surface, UiSurfaceInputPolicy::Disabled);

    f.Click(Vec2d{ 300.0f, 200.0f });
    EXPECT_FALSE(f.LastConsumed);
    EXPECT_FALSE(f.Raised(kPressed));
    EXPECT_FALSE(f.Ui->Capture().Mouse);
    EXPECT_FALSE(f.Ui->Capture().Keyboard);

    // The host mapped its own point and asks what is there: answered.
    const UiElementRef under = f.Ui->ElementAt(f.Surface, Vec2d{ 300.0f, 200.0f });
    ASSERT_TRUE(under.IsValid());
    EXPECT_EQ(f.Ui->DescribeElement(under)->Id, "button");
}

TEST(UiSurfaceInputPolicy, PointerDeliversClicksButNoKeysOrText)
{
    Fixture f;
    f.Ui->SetSurfaceInputPolicy(f.Surface, UiSurfaceInputPolicy::Pointer);

    f.Click(Vec2d{ 300.0f, 200.0f });
    EXPECT_TRUE(f.Raised(kPressed));

    // Focus the field by clicking it, then type: under Pointer the text is
    // never delivered and the keyboard is never claimed.
    f.Click(Vec2d{ 400.0f, 640.0f });
    f.Type("abc");
    EXPECT_FALSE(f.LastConsumed);
    EXPECT_EQ(std::string(f.Ui->GetValue(f.Screen, kName).AsString()), "");
    EXPECT_FALSE(f.Ui->Capture().Keyboard);

    // Granting Full, the same keystrokes land.
    f.Ui->SetSurfaceInputPolicy(f.Surface, UiSurfaceInputPolicy::Full);
    f.Click(Vec2d{ 400.0f, 640.0f });
    f.Type("abc");
    EXPECT_TRUE(f.LastConsumed);
    EXPECT_EQ(std::string(f.Ui->GetValue(f.Screen, kName).AsString()), "abc");
}

TEST(UiSurfaceInputPolicy, TheDefaultIsFull)
{
    Fixture f;
    EXPECT_EQ(f.Ui->GetSurfaceInputPolicy(f.Surface), UiSurfaceInputPolicy::Full);
    EXPECT_EQ(f.Ui->GetSurfaceInputPolicy(UiSurfaceId{}), UiSurfaceInputPolicy::Disabled)
        << "a surface that does not exist takes nothing";
}
