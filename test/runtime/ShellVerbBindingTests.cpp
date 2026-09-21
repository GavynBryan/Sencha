#include <gtest/gtest.h>

#include <app/BackRouter.h>
#include <app/EngineVerbs.h>
#include <app/PauseMenu.h>
#include <app/PauseState.h>
#include <app/ShellVerbs.h>
#include <assets/data/DataAssetCache.h>
#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageSerializer.h>
#include <authored/VerbBindingData.h>
#include <authored/VerbBindingSet.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <input/InputContextSet.h>
#include <runtime/RuntimeFrameLoop.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <SDL3/SDL.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

// The stock menu's behaviour coming from binding data.
//
// What Resume and Quit do is a record in the engine's own shell.bindings asset,
// resolved against the World's catalog like any other content. The things that
// must not regress in the process are the ones that were true before: a native
// handler still overrides an entry, a disabled entry still runs nothing, and a
// click queued before a reorder still means the row the player pressed.

namespace
{
std::vector<std::byte> BytesOf(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

// A root page shaped like the shipped one -- same model, same action, same
// repeated rows -- with a layout this test owns, so a click lands on a known
// row without asserting anything about how the product's chrome is styled.
constexpr std::string_view kMarkup = R"RML(<rml>
<head><link type="text/rcss" href="shell.rcss"/></head>
<body data-model="pause">
    <div id="title">{{title}}</div>
    <div id="entries">
        <div class="entry" data-for="entry : entries"
             data-event-click="pause_activate(it_index)" tab-index="auto">{{entry}}</div>
    </div>
</body>
</rml>)RML";

constexpr float kRowTop = 100.0f;
constexpr float kRowHeight = 40.0f;

constexpr std::string_view kStyle = R"(
body { display: block; width: 100%; height: 100%; pointer-events: none; }
#title { display: block; position: absolute; left: 0px; top: 0px;
         width: 300px; height: 40px; }
#entries { display: block; position: absolute; left: 0px; top: 100px;
           width: 300px; height: 400px; pointer-events: auto; }
.entry { display: block; width: 300px; height: 40px; pointer-events: auto;
         tab-index: auto; }
)";

[[nodiscard]] UiPackage MakePackage()
{
    UiPackage package;
    package.RootDocumentName = "shell.rml";

    UiPackageBlob root;
    root.VirtualName = "shell.rml";
    root.SourcePath = "ui/shell.rml";
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(kMarkup);
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = "shell.rcss";
    sheet.SourcePath = "ui/shell.rcss";
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(kStyle);
    package.Blobs.push_back(std::move(sheet));
    return package;
}

SDL_Event MouseButton(std::uint32_t type, float x, float y)
{
    SDL_Event event{};
    event.type = type;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    return event;
}

void ClickRow(UiService& ui, std::size_t row)
{
    const float y = kRowTop + kRowHeight * static_cast<float>(row) + kRowHeight * 0.5f;
    SDL_Event move{};
    move.type = SDL_EVENT_MOUSE_MOTION;
    move.motion.x = 150.0f;
    move.motion.y = y;
    (void)ui.ProcessPlatformEvent(move);
    (void)ui.ProcessPlatformEvent(MouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 150.0f, y));
    (void)ui.ProcessPlatformEvent(MouseButton(SDL_EVENT_MOUSE_BUTTON_UP, 150.0f, y));
}

// Stands in for the host's exit path: the engine's own quit operation needs an
// Engine, and what is being proved here is that the menu reaches an operation
// at all, not what Engine::RequestExit does with it.
class ExitRecorder
{
public:
    VerbAdmission Invoke(const VerbInvocation&)
    {
        ++Requests;
        return VerbAdmission::Accepted;
    }

    int Requests = 0;
};

class ShellVerbHost
{
public:
    ShellVerbHost()
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        std::random_device rd;
        Root = std::filesystem::temp_directory_path()
            / ("sencha_shell_verb_test_" + std::to_string(rd()));
        std::filesystem::create_directories(Root / "ui");

        std::vector<std::byte> bytes;
        EXPECT_TRUE(WriteSuiToBytes(MakePackage(), bytes));
        std::ofstream file(Root / "ui" / "shell.sui", std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        file.close();

        // The test's own document root, and the engine's real content beside it
        // so the shipped binding asset is the one under test.
        ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        const std::string engineRoot = std::string(SENCHA_REPO_ROOT) + "/engine/assets";
        ScanAssetsDirectory(engineRoot, Assets.Registry, Assets.Assets.Kinds());

        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages, Assets.Fonts,
                                         nullptr, nullptr);
        Surface = Ui->CreateSurface("shell", RenderExtent{ 800, 600 });

        Verbs = &InstallVerbRegistry(Entities);
        EXPECT_TRUE(DeclareEngineVerbs(*Verbs));

        Menu = std::make_unique<PauseMenu>(*Ui, Surface, Pause, Router);
        Menu->Model().SetRootPage("asset://ui/shell.sui");
        Menu->Model().InstallDefaults("Exit to Desktop");
    }

    ~ShellVerbHost()
    {
        Menu.reset();
        Controller.reset();
        if (Ui != nullptr)
            Ui->Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    ShellVerbHost(const ShellVerbHost&) = delete;
    ShellVerbHost& operator=(const ShellVerbHost&) = delete;

    // Loads the engine's shipped binding asset and puts the stock entries on it,
    // which is exactly what Engine::Run's composition does.
    [[nodiscard]] bool InstallAuthoredBehaviour()
    {
        const AssetLease lease = Assets.Assets.LoadLease(kShellBindingsAsset, AssetType::Data);
        if (!lease.IsValid())
            return false;
        BindingLease = DataAssetCacheHandle(&Assets.DataAssets,
                                            DataAssetHandle::FromToken(lease.OpaqueToken()));
        const auto* library = Assets.DataAssets.TryGet<VerbBindingLibrary>(
            BindingLease.GetToken(), std::string(kVerbBindingsTypeName));
        if (library == nullptr)
            return false;

        std::vector<std::string> errors;
        Bindings.Instantiate(*library, MakeVerbBindingEnvironment(Entities), errors);
        EXPECT_TRUE(errors.empty()) << (errors.empty() ? std::string{} : errors.front());

        Controller = std::make_unique<VerbDispatcher>(*Verbs);
        Resume = std::make_unique<RuntimeResumeOperation>(*Menu);
        ResumeToken = Controller->Bind(Verbs->Find(kRuntimeResumeVerb), *Resume);
        QuitToken = Controller->Bind(Verbs->Find(kApplicationQuitVerb), Exit);

        Menu->SetVerbBindings(Controller.get(), &Bindings);
        return Menu->Model().SetBinding(kPauseResume, MakeVerbBindingKey(kShellResumeBinding))
            && Menu->Model().SetBinding(kPauseExit, MakeVerbBindingKey(kShellQuitBinding));
    }

    void Frame()
    {
        Menu->Update(Runtime, Contexts);
        Ui->Update();
    }

    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    std::filesystem::path Root;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
    UiSurfaceId Surface;
    RuntimeFrameLoop Runtime;
    InputContextSet Contexts;
    PauseState Pause;
    BackRouter Router;
    std::unique_ptr<PauseMenu> Menu;

    World Entities;
    VerbRegistry* Verbs = nullptr;
    VerbBindingSet Bindings;
    DataAssetCacheHandle BindingLease;
    std::unique_ptr<VerbDispatcher> Controller;
    std::unique_ptr<RuntimeResumeOperation> Resume;
    ExitRecorder Exit;
    VerbBindingToken ResumeToken;
    VerbBindingToken QuitToken;
};
}

TEST(ShellVerbBindings, TheShippedAssetResolvesAgainstTheEnginesOwnVocabulary)
{
    ShellVerbHost host;
    ASSERT_TRUE(host.InstallAuthoredBehaviour())
        << "the engine ships shell.bindings.sdata and both stock records must resolve";

    const CompiledVerbBinding* resume = host.Bindings.Find(kShellResumeBinding);
    const CompiledVerbBinding* quit = host.Bindings.Find(kShellQuitBinding);
    ASSERT_NE(resume, nullptr);
    ASSERT_NE(quit, nullptr);
    EXPECT_EQ(resume->Verb, host.Verbs->Find(kRuntimeResumeVerb));
    EXPECT_EQ(quit->Verb, host.Verbs->Find(kApplicationQuitVerb));
    // No arguments and no inputs: what resuming does to the world is PauseState's
    // answer, not a caller's choice.
    EXPECT_TRUE(resume->Inputs.empty());
    EXPECT_EQ(resume->Constants.Size(), 0u);
}

TEST(ShellVerbBindings, ResumeThroughTheAuthoredBindingClosesTheShellInTheSameFrame)
{
    ShellVerbHost host;
    ASSERT_TRUE(host.InstallAuthoredBehaviour());

    host.Menu->Open();
    host.Frame();
    ASSERT_TRUE(host.Menu->IsOpen());
    ASSERT_TRUE(host.Pause.IsPaused());

    ClickRow(*host.Ui, 0);
    // One frame, no fixed ticks: the resume is admitted during the drain and the
    // pages close after it, which is what stops a handler destroying the model
    // it is being dispatched from.
    host.Frame();

    EXPECT_EQ(host.Menu->LastAuthoredAdmission(), VerbAdmission::Accepted);
    EXPECT_FALSE(host.Menu->IsOpen());
    EXPECT_FALSE(host.Pause.IsPaused());
}

TEST(ShellVerbBindings, TheExitEntryReachesTheHostsOperation)
{
    ShellVerbHost host;
    ASSERT_TRUE(host.InstallAuthoredBehaviour());

    host.Menu->Open();
    host.Frame();
    // Resume, then the way out: Options is absent without a page behind it.
    ASSERT_EQ(host.Menu->Model().Entries().size(), 2u);

    ClickRow(*host.Ui, 1);
    host.Frame();

    EXPECT_EQ(host.Exit.Requests, 1);
    EXPECT_EQ(host.Menu->LastAuthoredAdmission(), VerbAdmission::Accepted);
    // Asking to leave is not leaving: the shell stays up, and what happens next
    // is the host's answer.
    EXPECT_TRUE(host.Menu->IsOpen());
}

TEST(ShellVerbBindings, ANativeHandlerReplacesTheAuthoredBehaviourRatherThanRunningBesideIt)
{
    ShellVerbHost host;
    ASSERT_TRUE(host.InstallAuthoredBehaviour());

    int native = 0;
    (void)host.Menu->Model().SetHandler(kPauseExit, [&native](PauseMenuContext&) { ++native; });
    EXPECT_FALSE(host.Menu->Model().Binding(kPauseExit).IsValid())
        << "an entry with a native handler kept its authored binding, so it does both";

    host.Menu->Open();
    host.Frame();
    ClickRow(*host.Ui, 1);
    host.Frame();

    EXPECT_EQ(native, 1);
    EXPECT_EQ(host.Exit.Requests, 0);
}

TEST(ShellVerbBindings, AnAuthoredBindingReplacesANativeHandler)
{
    ShellVerbHost host;
    int native = 0;
    (void)host.Menu->Model().SetHandler(kPauseExit, [&native](PauseMenuContext&) { ++native; });
    ASSERT_TRUE(host.InstallAuthoredBehaviour());

    EXPECT_EQ(host.Menu->Model().Handler(kPauseExit), nullptr)
        << "assigning authored behaviour left a native handler that would also run";

    host.Menu->Open();
    host.Frame();
    ClickRow(*host.Ui, 1);
    host.Frame();

    EXPECT_EQ(native, 0);
    EXPECT_EQ(host.Exit.Requests, 1);
}

TEST(ShellVerbBindings, ADisabledEntryRunsNothing)
{
    ShellVerbHost host;
    ASSERT_TRUE(host.InstallAuthoredBehaviour());
    ASSERT_TRUE(host.Menu->Model().SetEnabled(kPauseExit, false));

    host.Menu->Open();
    host.Frame();
    ClickRow(*host.Ui, 1);
    host.Frame();

    EXPECT_EQ(host.Exit.Requests, 0);
    EXPECT_TRUE(host.Menu->IsOpen());
}

TEST(ShellVerbBindings, AQueuedClickKeepsTheMeaningTheRowHadWhenItWasPressed)
{
    ShellVerbHost host;
    ASSERT_TRUE(host.InstallAuthoredBehaviour());

    host.Menu->Open();
    host.Frame();
    ASSERT_EQ(host.Menu->Model().Entries()[0].Command, kPauseResume);

    // Pressed the second row, which is the way out.
    ClickRow(*host.Ui, 1);

    // The game reorders the menu before the frame that drains it. The player
    // pressed a row in the list they were looking at; nothing about that press
    // says "whatever ends up second".
    ASSERT_TRUE(host.Menu->Model().MoveBefore(kPauseExit, kPauseResume));
    host.Menu->MarkModelChanged();
    host.Frame();

    EXPECT_EQ(host.Exit.Requests, 1);
    // And not the resume that moved into the pressed row's position.
    EXPECT_TRUE(host.Menu->IsOpen());
    EXPECT_TRUE(host.Pause.IsPaused());
}

TEST(ShellVerbBindings, AHostThatComposedNoVocabularyReportsTheEntryUnavailable)
{
    // A tool, a dedicated server, a build with no engine content: the entry
    // still exists and still presents, and pressing it reaches nothing rather
    // than reaching through a null owner.
    ShellVerbHost host;
    ASSERT_TRUE(host.Menu->Model().SetBinding(kPauseResume,
                                              MakeVerbBindingKey(kShellResumeBinding)));

    host.Menu->Open();
    host.Frame();
    ClickRow(*host.Ui, 0);
    host.Frame();

    EXPECT_EQ(host.Menu->LastAuthoredAdmission(), VerbAdmission::Unavailable);
    EXPECT_TRUE(host.Menu->IsOpen());
    EXPECT_TRUE(host.Pause.IsPaused());
}

TEST(ShellVerbBindings, AuthoredResumeLeavesTheNetworkSessionsPausePolicyAlone)
{
    // A stock menu never freezes a live session, at either end. The authored
    // path asks the shell for the same deferred resume a native handler does,
    // so PauseState decides this exactly as it did before.
    ShellVerbHost host;
    ASSERT_TRUE(host.InstallAuthoredBehaviour());
    host.Pause.SetSessionLive(true);

    host.Menu->Open();
    host.Frame();
    EXPECT_EQ(host.Pause.Effective(), PausePolicy::InputOnly);

    ClickRow(*host.Ui, 0);
    host.Frame();

    EXPECT_FALSE(host.Menu->IsOpen());
    EXPECT_FALSE(host.Pause.IsPaused());
    EXPECT_EQ(host.Pause.Effective(), PausePolicy::InputOnly);
}

TEST(ShellVerbBindings, ResumingWithNothingOpenIsRefusedRatherThanSilentlyAccepted)
{
    ShellVerbHost host;
    ASSERT_TRUE(host.InstallAuthoredBehaviour());

    const CompiledVerbBinding* resume = host.Bindings.Find(kShellResumeBinding);
    ASSERT_NE(resume, nullptr);
    EXPECT_EQ(host.Controller->Invoke(*resume, {}).Status, VerbAdmission::Refused);
}
