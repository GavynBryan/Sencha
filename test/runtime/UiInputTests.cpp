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
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

// Input, from the platform to a semantic action, without a window or a device.
//
// What these protect is the shape of the boundary rather than the plumbing: a
// click becomes something the host declared, the UI reports what it consumed
// instead of hiding it, and a modal takes presentation focus without touching
// anything the application owns.

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
             / ("sencha_ui_input_test_" + std::to_string(rd()));
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

// A button occupying a known rectangle, so a synthesised click can be aimed at
// it and at a point outside it.
constexpr std::string_view kMarkup = R"(<rml>
<head><link type="text/rcss" href="menu.rcss"/></head>
<body data-model="menu">
    <div id="quit" data-event-click="menu_quit"/>
</body>
</rml>)";

// Two authoring contracts, both stated here because both are easy to forget and
// silent when missed:
//
//   pointer-events: none on a full-screen root, or a HUD covering the window
//   counts as "the pointer is over the UI" and quietly takes the mouse from the
//   game. Interactive elements opt back in.
//
//   tab-index: auto to be focusable at all. Without it an element is not in the
//   tab order, so it cannot be reached by a controller or a keyboard.
constexpr std::string_view kStyle = R"(
body { display: block; width: 100%; height: 100%; pointer-events: none; }
#quit { display: block; position: absolute; left: 100px; top: 100px;
        width: 200px; height: 80px; background-color: #ffffffff;
        pointer-events: auto; tab-index: auto; }
)";

UiPackage MakePackage(std::string_view rootName = "menu.rml", std::string_view markup = kMarkup,
                      std::string_view style = kStyle, std::string_view sheetName = "menu.rcss")
{
    UiPackage package;
    package.RootDocumentName = std::string(rootName);

    UiPackageBlob root;
    root.VirtualName = std::string(rootName);
    root.SourcePath = "ui/" + std::string(rootName);
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(markup);
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = std::string(sheetName);
    sheet.SourcePath = "ui/" + std::string(sheetName);
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(style);
    package.Blobs.push_back(std::move(sheet));
    return package;
}

UiScreenDesc MakeDesc(std::string_view path = "asset://ui/menu.sui")
{
    UiScreenDesc desc;
    desc.PackagePath = std::string(path);
    desc.ModelName = "menu";
    desc.Actions = { "menu_quit" };
    return desc;
}

SDL_Event MouseMove(float x, float y)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

SDL_Event MouseButton(uint32_t type, float x, float y)
{
    SDL_Event event{};
    event.type = type;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    return event;
}

class UiTestHost
{
public:
    explicit UiTestHost(const TempAssetRoot& root)
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        ScanAssetsDirectory(root.PathString(), Assets.Registry, Assets.Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages,
                                         Assets.Fonts, nullptr, nullptr);
    }
    ~UiTestHost() { if (Ui != nullptr) Ui->Shutdown(); }
    UiService& Service() { return *Ui; }

private:
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
};

// One click, aimed. Press and release both have to land on the element for the
// engine to call it a click.
void ClickAt(UiService& ui, float x, float y)
{
    (void)ui.ProcessPlatformEvent(MouseMove(x, y));
    (void)ui.ProcessPlatformEvent(MouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y));
    (void)ui.ProcessPlatformEvent(MouseButton(SDL_EVENT_MOUSE_BUTTON_UP, x, y));
}

void WritePackage(const TempAssetRoot& root, std::string_view relPath,
                  std::string_view rootName = "menu.rml", std::string_view markup = kMarkup,
                  std::string_view style = kStyle, std::string_view sheetName = "menu.rcss")
{
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSuiToBytes(MakePackage(rootName, markup, style, sheetName), bytes));
    root.WriteBytes(relPath, bytes);
}
} // namespace

TEST(UiInput, AClickOnAButtonBecomesTheActionTheHostDeclared)
{
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();
    ASSERT_TRUE(ui.IsReady());

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());
    ui.Update();

    ClickAt(ui, 150.0f, 130.0f);
    ui.Update();

    const std::vector<UiAction> actions = ui.DrainActions();
    ASSERT_EQ(actions.size(), 1u) << "a click on the button produced no action";
    EXPECT_EQ(actions[0].Screen, screen) << "the action did not name the screen that raised it";
    EXPECT_EQ(actions[0].Id, UiActionIdAt(0));

    // And taken exactly once.
    EXPECT_TRUE(ui.DrainActions().empty());
}

TEST(UiInput, AClickOutsideTheButtonRaisesNothing)
{
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    ASSERT_TRUE(ui.OpenScreen(surface, MakeDesc()).IsValid());
    ui.Update();

    ClickAt(ui, 600.0f, 500.0f);
    ui.Update();
    EXPECT_TRUE(ui.DrainActions().empty());
}

TEST(UiInput, ThePointerOverAPanelIsReportedAsCaptured)
{
    // Reported, not hidden: the device snapshot still has the click. This is
    // what tells a raw reader it was not aimed at them.
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    ASSERT_TRUE(ui.OpenScreen(surface, MakeDesc()).IsValid());
    ui.Update();

    (void)ui.ProcessPlatformEvent(MouseMove(150.0f, 130.0f));
    EXPECT_TRUE(ui.Capture().Mouse) << "the pointer is over a panel and nothing said so";

    (void)ui.ProcessPlatformEvent(MouseMove(600.0f, 500.0f));
    EXPECT_FALSE(ui.Capture().Mouse) << "the pointer left the panel and capture stuck";
}

TEST(UiInput, AnOpenScreenDoesNotTakeTheKeyboardByItself)
{
    // A HUD is open for the whole game and a pause menu still has to let the
    // console key through. Only a focused text field takes the keyboard, and
    // suppressing gameplay controls is the host's InputContextLease, not this.
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    ASSERT_TRUE(ui.OpenScreen(surface, MakeDesc()).IsValid());
    ui.Update();

    EXPECT_FALSE(ui.Capture().Keyboard);
    ClickAt(ui, 150.0f, 130.0f);
    ui.Update();
    EXPECT_FALSE(ui.Capture().Keyboard)
        << "clicking a plain button took the keyboard from the game";
}

TEST(UiInput, EventsAreOfferedToTheTopmostSurfaceFirst)
{
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();

    // Two surfaces, same document. The later one is on top.
    const UiSurfaceId lower = ui.CreateSurface("lower", RenderExtent{ 800, 600 });
    const UiSurfaceId upper = ui.CreateSurface("upper", RenderExtent{ 800, 600 });
    const UiScreenHandle lowerScreen = ui.OpenScreen(lower, MakeDesc());
    const UiScreenHandle upperScreen = ui.OpenScreen(upper, MakeDesc());
    ASSERT_TRUE(lowerScreen.IsValid());
    ASSERT_TRUE(upperScreen.IsValid());
    ui.Update();

    ClickAt(ui, 150.0f, 130.0f);
    ui.Update();

    const std::vector<UiAction> actions = ui.DrainActions();
    ASSERT_EQ(actions.size(), 1u) << "the click reached both surfaces instead of stopping";
    EXPECT_EQ(actions[0].Screen, upperScreen)
        << "the click went to the surface underneath the one on top";
}

TEST(UiInput, AWindowsOwnSurfaceIsAboveAnOffscreenOneShownBeneathIt)
{
    // An editor shows a previewed document under its own panels: the preview's
    // offscreen surface is placed where its picture is, and was made after
    // the window's surface, yet a click on the panel above it is the panel's.
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId window = ui.CreateSurface("window", RenderExtent{ 800, 600 });
    const UiSurfaceId preview = ui.CreateSurface("preview", RenderExtent{ 800, 600 });
    ui.SetSurfaceDestination(preview, UiSurfaceDestination::Offscreen);
    ui.SetSurfacePlacement(preview, Rect2d(0.0f, 0.0f, 800.0f, 600.0f));
    const UiScreenHandle windowScreen = ui.OpenScreen(window, MakeDesc());
    const UiScreenHandle previewScreen = ui.OpenScreen(preview, MakeDesc());
    ASSERT_TRUE(windowScreen.IsValid());
    ASSERT_TRUE(previewScreen.IsValid());
    ui.Update();

    ClickAt(ui, 150.0f, 130.0f);
    ui.Update();
    const std::vector<UiAction> actions = ui.DrainActions();
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].Screen, windowScreen) << "the offscreen surface took a click aimed at the window's own";
    EXPECT_FALSE(ui.IsPointerOver(preview)) << "the surface below was told the pointer is not over it";
}

TEST(UiInput, NavigationComesFromTheHostsActionsNotFromKeys)
{
    // The point is the direction of the dependency: the host decides that some
    // mapped action means "accept", and this turns that into activation. No
    // document names a key or a gamepad button anywhere in the path.
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());
    ui.Update();

    // Move focus onto the button, then activate it.
    ui.Navigate(surface, UiNavigation::Next);
    ui.Update();
    ui.Navigate(surface, UiNavigation::Accept);
    ui.Update();

    const std::vector<UiAction> actions = ui.DrainActions();
    ASSERT_EQ(actions.size(), 1u)
        << "tab-then-accept did not activate the button, so a controller cannot either";
    EXPECT_EQ(actions[0].Id, UiActionIdAt(0));
}

TEST(UiInput, NavigatingADestroyedSurfaceIsANoOpRatherThanACrash)
{
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    ASSERT_TRUE(ui.OpenScreen(surface, MakeDesc()).IsValid());
    ui.DestroySurface(surface);

    ui.Navigate(surface, UiNavigation::Accept);
    EXPECT_TRUE(ui.DrainActions().empty());
}

TEST(UiInput, EventsReachNothingOnceEveryScreenIsClosed)
{
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());
    ui.Update();

    ui.CloseScreen(screen);
    ui.Update();

    ClickAt(ui, 150.0f, 130.0f);
    ui.Update();
    EXPECT_TRUE(ui.DrainActions().empty())
        << "a closed screen still answered a click";
    EXPECT_FALSE(ui.Capture().Mouse);
}

TEST(UiInput, ClosingAScreenTakesTheActionsItHadNotDeliveredYet)
{
    // A page stack pops a screen in response to one of its own actions, which
    // is the moment a second queued action from the same screen would be
    // stranded: closing bumps the generation, so its handle stops resolving and
    // DrainActions(screen) can never match it again. It would sit in the queue
    // until some unrelated controller drained everything, and then arrive
    // naming a screen that no longer exists.
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());
    ui.Update();

    ClickAt(ui, 150.0f, 130.0f);
    ui.CloseScreen(screen);

    EXPECT_TRUE(ui.DrainActions().empty())
        << "an action outlived the screen that raised it";
}

TEST(UiInput, AnotherScreensQueuedActionsSurviveACloseBesideThem)
{
    // The purge is scoped to the screen that closed. A stack popping its top
    // page must not take the HUD's queued actions with it.
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle lower = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(lower.IsValid());
    ui.Update();
    ClickAt(ui, 150.0f, 130.0f);

    // A different model name, because a context holds one model per name: two
    // screens on a surface cannot both declare "menu". The pause stack's pages
    // are named apart for the same reason.
    const UiScreenHandle upper = ui.OpenScreen(surface, "asset://ui/menu.sui");
    ASSERT_TRUE(upper.IsValid());
    ui.Update();
    ui.CloseScreen(upper);

    const std::vector<UiAction> left = ui.DrainActions();
    ASSERT_EQ(left.size(), 1u) << "the lower screen's action was taken too";
    EXPECT_EQ(left.front().Screen, lower);
}

namespace
{
// A click in a given window: every event carries the id the platform stamps.
void ClickInWindow(UiService& ui, std::uint32_t window, float x, float y)
{
    SDL_Event move = MouseMove(x, y);
    move.motion.windowID = window;
    SDL_Event down = MouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
    down.button.windowID = window;
    SDL_Event up = MouseButton(SDL_EVENT_MOUSE_BUTTON_UP, x, y);
    up.button.windowID = window;
    (void)ui.ProcessPlatformEvent(move);
    (void)ui.ProcessPlatformEvent(down);
    (void)ui.ProcessPlatformEvent(up);
}
} // namespace

TEST(UiInput, ASurfaceHearsOnlyItsOwnWindow)
{
    // A workspace moved into a window of its own takes its surfaces with it.
    // The same point in two windows is two different places, so a click in
    // one must not press a button that happens to sit there in the other.
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();

    constexpr std::uint32_t kDetached = 7;
    const UiSurfaceId primary = ui.CreateSurface("primary", RenderExtent{ 800, 600 });
    const UiSurfaceId detached = ui.CreateSurface("detached", RenderExtent{ 800, 600 });
    ui.SetSurfaceWindow(detached, kDetached);
    const UiScreenHandle primaryScreen = ui.OpenScreen(primary, MakeDesc());
    const UiScreenHandle detachedScreen = ui.OpenScreen(detached, MakeDesc());
    ASSERT_TRUE(primaryScreen.IsValid());
    ASSERT_TRUE(detachedScreen.IsValid());
    ui.Update();

    ClickInWindow(ui, kDetached, 150.0f, 130.0f);
    ui.Update();
    std::vector<UiAction> actions = ui.DrainActions();
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].Screen, detachedScreen) << "the detached window's click pressed the primary's button";

    ClickInWindow(ui, 0, 150.0f, 130.0f);
    ui.Update();
    actions = ui.DrainActions();
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].Screen, primaryScreen) << "a primary-window click reached the detached window";
}

TEST(UiInput, CaptureIsReportedPerWindow)
{
    // A host routes each window's input to that window's own shortcuts, so it
    // needs to know whether the UI took the pointer in that window, not in any.
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();

    constexpr std::uint32_t kDetached = 7;
    const UiSurfaceId detached = ui.CreateSurface("detached", RenderExtent{ 800, 600 });
    ui.SetSurfaceWindow(detached, kDetached);
    ASSERT_TRUE(ui.OpenScreen(detached, MakeDesc()).IsValid());
    ui.Update();

    SDL_Event move = MouseMove(150.0f, 130.0f);
    move.motion.windowID = kDetached;
    (void)ui.ProcessPlatformEvent(move);

    EXPECT_TRUE(ui.Capture(kDetached).Mouse);
    EXPECT_FALSE(ui.Capture(0).Mouse) << "the primary reported a pointer that is in another window";
    EXPECT_TRUE(ui.Capture().Mouse) << "the all-windows answer lost the detached window's pointer";
}

TEST(UiInput, AFrameNamesItsWindow)
{
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId detached = ui.CreateSurface("detached", RenderExtent{ 800, 600 });
    ui.SetSurfaceWindow(detached, 7);
    ASSERT_TRUE(ui.OpenScreen(detached, MakeDesc()).IsValid());
    ui.Update();
    ui.ExtractRender();

    ASSERT_EQ(ui.Frames().size(), 1u);
    EXPECT_EQ(ui.Frames().front().WindowId, 7u)
        << "the primary's render feature would draw another window's surface";
}

namespace
{
// A list that holds more rows than it shows, beside a panel that scrolls
// nothing, over a root the pointer passes through.
std::string ScrollingMarkup()
{
    std::string rows;
    for (int i = 0; i < 20; ++i)
        rows += "<div class=\"row\" id=\"row" + std::to_string(i) + "\"/>";
    return R"(<rml>
<head><link type="text/rcss" href="menu.rcss"/></head>
<body data-model="menu">
    <div id="list" data-event-mousescroll="menu_quit">)" + rows + R"(</div>
    <div id="panel"/>
</body>
</rml>)";
}

constexpr std::string_view kScrollingStyle = R"(
body { display: block; width: 100%; height: 100%; pointer-events: none; }
#list { display: block; position: absolute; left: 0px; top: 0px; width: 200px; height: 100px;
        overflow-y: auto; pointer-events: auto; }
#list scrollbarvertical { width: 8px; }
#list scrollbarvertical sliderbar { min-height: 8px; }
.row { display: block; height: 40px; }
#panel { display: block; position: absolute; left: 300px; top: 0px; width: 100px; height: 100px;
         pointer-events: auto; }
)";

SDL_Event Wheel(float x, float y, float notches)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.mouse_x = x;
    event.wheel.mouse_y = y;
    event.wheel.y = notches;
    event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    return event;
}

struct ScrollingScreen
{
    TempAssetRoot Root;
    std::unique_ptr<UiTestHost> Host;
    UiScreenHandle Screen;

    ScrollingScreen()
    {
        WritePackage(Root, "ui/menu.sui", "menu.rml", ScrollingMarkup(), kScrollingStyle);
        Host = std::make_unique<UiTestHost>(Root);
        UiService& ui = Host->Service();
        const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
        Screen = ui.OpenScreen(surface, MakeDesc());
        ui.Update();
    }
};
} // namespace

TEST(UiInput, TheWheelOverTheDocumentIsTheDocumentsWhetherOrNotAnythingScrolls)
{
    // A panel with nothing to scroll still sits over the world; a wheel turned
    // over it must not zoom the camera behind it.
    ScrollingScreen fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());
    UiService& ui = fixture.Host->Service();

    (void)ui.ProcessPlatformEvent(MouseMove(350.0f, 50.0f));
    EXPECT_TRUE(ui.ProcessPlatformEvent(Wheel(350.0f, 50.0f, -1.0f)))
        << "a wheel over a panel with nothing to scroll fell through to what is beneath";

    (void)ui.ProcessPlatformEvent(MouseMove(100.0f, 50.0f));
    EXPECT_TRUE(ui.ProcessPlatformEvent(Wheel(100.0f, 50.0f, -1.0f)));

    (void)ui.ProcessPlatformEvent(MouseMove(600.0f, 400.0f));
    EXPECT_FALSE(ui.ProcessPlatformEvent(Wheel(600.0f, 400.0f, -1.0f)))
        << "a wheel over the pass-through root was kept from what is beneath";
}

TEST(UiInput, CtrlAndTheWheelReachOnlyTheDocumentsListener)
{
    ScrollingScreen fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());
    UiService& ui = fixture.Host->Service();

    (void)ui.ProcessPlatformEvent(MouseMove(100.0f, 50.0f));
    SDL_SetModState(SDL_KMOD_LCTRL);
    const bool consumed = ui.ProcessPlatformEvent(Wheel(100.0f, 50.0f, -3.0f));
    SDL_SetModState(SDL_KMOD_NONE);
    ui.Update();

    EXPECT_TRUE(consumed);
    EXPECT_EQ(ui.DrainActions().size(), 1u) << "the listener did not hear Ctrl and the wheel";
    const std::optional<UiScrollBox> box = ui.MeasureScroll(fixture.Screen, "list");
    ASSERT_TRUE(box.has_value());
    EXPECT_EQ(box->Top, 0.0f) << "Ctrl and the wheel scrolled as well as zooming";
}

TEST(UiInput, ScrollingIntoViewBringsAFarRowWithinTheList)
{
    ScrollingScreen fixture;
    ASSERT_TRUE(fixture.Screen.IsValid());
    UiService& ui = fixture.Host->Service();

    const std::optional<UiScrollBox> before = ui.MeasureScroll(fixture.Screen, "list");
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(before->Top, 0.0f);
    EXPECT_EQ(before->ClientHeight, 100.0f);
    EXPECT_EQ(before->ScrollHeight, 800.0f) << "the list did not hold its rows' height";

    ASSERT_TRUE(ui.ScrollIntoView(fixture.Screen, "row10"));
    const std::optional<UiScrollBox> after = ui.MeasureScroll(fixture.Screen, "list");
    ASSERT_TRUE(after.has_value());
    // Nearest: the row's bottom edge meets the list's.
    EXPECT_EQ(after->Top, 10.0f * 40.0f + 40.0f - 100.0f);

    EXPECT_FALSE(ui.ScrollIntoView(fixture.Screen, "no_such_row"));
    EXPECT_FALSE(ui.MeasureScroll(fixture.Screen, "no_such_row").has_value());
}

namespace
{
// A handle dragged by its press, whose drag ending is the action. It sits
// clear of the button kMarkup puts at (100, 100, 200, 80).
constexpr std::string_view kDragMarkup = R"(<rml>
<head><link type="text/rcss" href="drag.rcss"/></head>
<body data-model="menu">
    <div id="handle" data-event-dragend="menu_quit"/>
</body>
</rml>)";

constexpr std::string_view kDragStyle = R"(
body { display: block; width: 100%; height: 100%; pointer-events: none; }
#handle { display: block; position: absolute; left: 400px; top: 300px; width: 200px; height: 80px;
          drag: drag; pointer-events: auto; }
)";

// A handle on one surface, a button on a surface above it.
struct DragUnderButton
{
    TempAssetRoot Root;
    std::unique_ptr<UiTestHost> Host;
    UiSurfaceId Lower;
    UiScreenHandle Dragged;

    DragUnderButton()
    {
        WritePackage(Root, "ui/drag.sui", "drag.rml", kDragMarkup, kDragStyle, "drag.rcss");
        WritePackage(Root, "ui/menu.sui");
        Host = std::make_unique<UiTestHost>(Root);
        UiService& ui = Host->Service();
        Lower = ui.CreateSurface("lower", RenderExtent{ 800, 600 });
        const UiSurfaceId upper = ui.CreateSurface("upper", RenderExtent{ 800, 600 });
        Dragged = ui.OpenScreen(Lower, MakeDesc("asset://ui/drag.sui"));
        EXPECT_TRUE(Dragged.IsValid());
        EXPECT_TRUE(ui.OpenScreen(upper, MakeDesc()).IsValid());
        ui.Update();
    }

    // Takes the handle and drags it over the upper surface's button.
    void DragOverTheButton()
    {
        UiService& ui = Host->Service();
        (void)ui.ProcessPlatformEvent(MouseMove(450.0f, 330.0f));
        ASSERT_TRUE(ui.ProcessPlatformEvent(MouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 450.0f, 330.0f)));
        (void)ui.ProcessPlatformEvent(MouseMove(420.0f, 320.0f));
        (void)ui.ProcessPlatformEvent(MouseMove(150.0f, 130.0f));
    }

    [[nodiscard]] std::size_t DragsEnded()
    {
        std::size_t ended = 0;
        for (const UiAction& action : Host->Service().DrainActions())
            ended += action.Screen == Dragged ? 1 : 0;
        return ended;
    }
};
} // namespace

TEST(UiInput, AReleaseOverAnotherSurfaceEndsTheDragItBelongsTo)
{
    DragUnderButton fixture;
    UiService& ui = fixture.Host->Service();
    fixture.DragOverTheButton();

    EXPECT_TRUE(ui.ProcessPlatformEvent(MouseButton(SDL_EVENT_MOUSE_BUTTON_UP, 150.0f, 130.0f)));
    ui.Update();
    EXPECT_EQ(fixture.DragsEnded(), 1u) << "the button above took the release, and the drag under it never ended";
    (void)ui.ProcessPlatformEvent(MouseMove(700.0f, 550.0f));
    EXPECT_FALSE(ui.Capture().Mouse) << "the handle's surface still holds the pointer";
}

TEST(UiInput, ASurfaceThatStopsTakingInputEndsTheDragItHeld)
{
    DragUnderButton fixture;
    UiService& ui = fixture.Host->Service();
    fixture.DragOverTheButton();

    ui.SetSurfaceInputPolicy(fixture.Lower, UiSurfaceInputPolicy::Disabled);
    ui.Update();
    EXPECT_EQ(fixture.DragsEnded(), 1u) << "the drag was left running where no release can reach it";
}

TEST(UiInput, AnEndedPressClicksNothing)
{
    // A press handed on, or cut off by the window losing focus, is let go
    // with nothing under the pointer.
    TempAssetRoot root;
    WritePackage(root, "ui/menu.sui");
    UiTestHost host(root);
    UiService& ui = host.Service();
    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    ASSERT_TRUE(ui.OpenScreen(surface, MakeDesc()).IsValid());
    ui.Update();

    (void)ui.ProcessPlatformEvent(MouseMove(150.0f, 130.0f));
    ASSERT_TRUE(ui.ProcessPlatformEvent(MouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 150.0f, 130.0f)));
    ui.EndPointerGesture(0);
    ui.Update();
    EXPECT_TRUE(ui.DrainActions().empty()) << "ending the press clicked the button under it";
    EXPECT_FALSE(ui.Capture().Mouse);

    // And the surface takes the next press as its own again.
    ClickAt(ui, 150.0f, 130.0f);
    ui.Update();
    EXPECT_EQ(ui.DrainActions().size(), 1u);
}
