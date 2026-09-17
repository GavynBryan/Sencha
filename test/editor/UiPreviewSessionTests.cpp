#include <gtest/gtest.h>

#include "authoring/UiPreviewSession.h"

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

//=============================================================================
// One document being looked at, driven the way the panels will drive it, with
// no panel anywhere near it.
//=============================================================================

namespace
{
std::vector<std::byte> BytesOf(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

struct Fixture
{
    std::filesystem::path Root;
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    std::unique_ptr<RuntimeAssets> Assets;
    std::unique_ptr<UiService> Ui;

    Fixture()
    {
        std::random_device rd;
        Root = std::filesystem::temp_directory_path() / ("sencha_preview_session_" + std::to_string(rd()));
        std::filesystem::create_directories(Root / "ui");

        UiPackage package;
        package.RootDocumentName = "doc.rml";
        UiPackageBlob root;
        root.VirtualName = "doc.rml";
        root.SourcePath = "ui/doc.rml";
        root.Kind = UiBlobKind::Document;
        root.Bytes = BytesOf(R"rml(<rml><head><link type="text/rcss" href="doc.rcss"/></head>
<body data-model="m"><div id="panel"><div id="button" data-event-click="pressed"/>{{title}}{{missing}}</div></body></rml>)rml");
        package.Blobs.push_back(std::move(root));
        UiPackageBlob sheet;
        sheet.VirtualName = "doc.rcss";
        sheet.SourcePath = "ui/doc.rcss";
        sheet.Kind = UiBlobKind::StyleSheet;
        sheet.Bytes = BytesOf("body { display: block; width: 100%; height: 100%; pointer-events: none; }"
                              "#panel { display: block; width: 50%; height: 100px; }"
                              "#button { display: block; width: 100px; height: 40px; pointer-events: auto; }");
        package.Blobs.push_back(std::move(sheet));
        std::vector<std::byte> bytes;
        EXPECT_TRUE(WriteSuiToBytes(package, bytes));
        std::ofstream(Root / "ui" / "doc.sui", std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

        Assets = std::make_unique<RuntimeAssets>(Logging, Serializers, RuntimeAssets::ReferenceOnly{});
        ScanAssetsDirectory(Root.generic_string(), Assets->Registry, Assets->Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets->Assets, Assets->UiPackages,
                                         Assets->Fonts, nullptr, nullptr);
    }
    ~Fixture()
    {
        Ui->Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    static UiPreviewModel Model()
    {
        UiPreviewModel m;
        m.ModelName = "m";
        m.Properties = { UiModelProperty{ "title", UiValue(std::string("T")), false } };
        m.Actions = { "pressed" };
        return m;
    }
};
}

TEST(UiPreviewSession, OpensOffscreenAndRelaysOutWithResolutionAndScale)
{
    Fixture f;
    UiPreviewSession session(*f.Ui);
    EXPECT_EQ(f.Ui->GetSurfaceDestination(session.Surface()), UiSurfaceDestination::Offscreen);

    ASSERT_TRUE(session.Open("asset://ui/doc.sui", Fixture::Model()));
    f.Ui->Update();
    const auto at1080 = f.Ui->MeasureElement(session.OpenScreen(), "panel");
    ASSERT_TRUE(at1080.has_value());
    EXPECT_FLOAT_EQ(at1080->Width, 960.0f) << "half of 1920";

    session.SetResolution(RenderExtent{ 1280, 720 });
    f.Ui->Update();
    EXPECT_FLOAT_EQ(f.Ui->MeasureElement(session.OpenScreen(), "panel")->Width, 640.0f);

    session.SetDisplayScale(2.0f);
    EXPECT_FLOAT_EQ(f.Ui->GetSurfaceScale(session.Surface()), 2.0f);
}

TEST(UiPreviewSession, PointerModeAndActivationBecomeTheSurfacesInputPolicy)
{
    Fixture f;
    UiPreviewSession session(*f.Ui);
    ASSERT_TRUE(session.Open("asset://ui/doc.sui", Fixture::Model()));

    EXPECT_EQ(f.Ui->GetSurfaceInputPolicy(session.Surface()), UiSurfaceInputPolicy::Pointer)
        << "interacting but not yet activated: pointer only";
    session.SetActivated(true);
    EXPECT_EQ(f.Ui->GetSurfaceInputPolicy(session.Surface()), UiSurfaceInputPolicy::Full);
    session.SetPointerMode(UiPreviewSession::PointerMode::Inspect);
    EXPECT_EQ(f.Ui->GetSurfaceInputPolicy(session.Surface()), UiSurfaceInputPolicy::Disabled)
        << "inspecting delivers nothing, activated or not";
    session.SetPointerMode(UiPreviewSession::PointerMode::Interact);
    EXPECT_EQ(f.Ui->GetSurfaceInputPolicy(session.Surface()), UiSurfaceInputPolicy::Full)
        << "activation survives a trip through inspect";
}

TEST(UiPreviewSession, PollIsTheOneDrainerAndKeepsHistories)
{
    Fixture f;
    UiPreviewSession session(*f.Ui);
    ASSERT_TRUE(session.Open("asset://ui/doc.sui", Fixture::Model()));
    f.Ui->Update();
    session.Poll();

    // The document reads {{missing}}, which the model lacks: in the history,
    // and gone from the engine's feed.
    bool sawMissing = false;
    for (const UiDiagnostic& d : session.Diagnostics())
        sawMissing |= d.Kind == UiDiagnosticKind::BindingMissing && d.Variable.value_or("") == "missing";
    EXPECT_TRUE(sawMissing);
    EXPECT_TRUE(f.Ui->DrainDiagnostics().empty()) << "a second drainer would find nothing";
    const std::size_t after = session.Diagnostics().size();
    session.Poll();
    EXPECT_EQ(session.Diagnostics().size(), after) << "nothing new, nothing added";

    // Actions the document raises land in the action history.
    SDL_Event move{};
    move.type = SDL_EVENT_MOUSE_MOTION;
    move.motion.x = 50.0f;
    move.motion.y = 20.0f;
    (void)f.Ui->ProcessPlatformEvent(move);
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.x = 50.0f;
    down.button.y = 20.0f;
    (void)f.Ui->ProcessPlatformEvent(down);
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    (void)f.Ui->ProcessPlatformEvent(up);
    f.Ui->Update();
    session.Poll();
    ASSERT_FALSE(session.Actions().empty()) << "the click on #button raised nothing";
    EXPECT_EQ(session.Actions().back().Id, UiActionIdAt(0));
}

TEST(UiPreviewSession, ReopenAppliesAnEditedModelAndCloseLeavesNothingOpen)
{
    Fixture f;
    UiPreviewSession session(*f.Ui);
    ASSERT_TRUE(session.Open("asset://ui/doc.sui", Fixture::Model()));
    f.Ui->Update();
    session.Poll();
    session.ClearDiagnostics();

    // Declaring the missing binding and reopening makes the miss go away.
    session.Model().Properties.push_back(UiModelProperty{ "missing", UiValue(std::string("now here")), false });
    ASSERT_TRUE(session.Reopen());
    f.Ui->Update();
    session.Poll();
    for (const UiDiagnostic& d : session.Diagnostics())
        EXPECT_NE(d.Kind, UiDiagnosticKind::BindingMissing) << d.Message;

    const UiScreenHandle screen = session.OpenScreen();
    session.Close();
    EXPECT_FALSE(session.IsOpen());
    EXPECT_FALSE(f.Ui->IsScreenOpen(screen));
}
