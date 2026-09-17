#include <gtest/gtest.h>

#include <app/BackRouter.h>
#include <app/PauseMenu.h>
#include <app/PauseState.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <input/InputContextSet.h>
#include <runtime/RuntimeFrameLoop.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <memory>
#include <string>
#include <vector>

// The page stack: what Back means at each depth, and what a page opened over
// the root owns. Driven against the documents the engine actually ships.

namespace
{
class ShellHost
{
public:
    ShellHost()
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        const std::string root = std::string(SENCHA_REPO_ROOT) + "/engine/assets";
        ScanAssetsDirectory(root, Assets.Registry, Assets.Assets.Kinds());
        ScanAssetsDirectory(root + "/.cooked", Assets.Registry, Assets.Assets.Kinds());
        RegisterCookedAssets(root, Assets.Registry);
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages,
                                         Assets.Fonts, nullptr, nullptr);
        Surface = Ui->CreateSurface("shell", RenderExtent{ 1280, 720 });
        Menu = std::make_unique<PauseMenu>(*Ui, Surface, Pause, Router);
        Menu->Model().InstallDefaults("Exit to Desktop");
    }
    ~ShellHost()
    {
        Menu.reset();
        if (Ui != nullptr)
            Ui->Shutdown();
    }

    void Frame()
    {
        Menu->Update(Runtime, Contexts);
        Ui->Update();
    }

    // A page shaped like the options one, without its settings.
    PauseMenu::Page MakePage(std::string modelName, std::vector<std::string>* activated = nullptr)
    {
        PauseMenu::Page page;
        page.Desc.PackagePath = "asset://ui/options.rml";
        page.Desc.ModelName = std::move(modelName);
        page.Desc.Modal = true;
        page.Desc.Properties = {
            UiModelProperty{ "title", UiValue(std::string("Options")) },
            UiModelProperty{ "hint", UiValue(std::string{}) },
        };
        page.Desc.RowLists = { "rows" };
        page.Desc.Actions = { "options_activate" };
        page.Publish = [this](UiScreenHandle screen) {
            std::vector<UiRow> rows;
            rows.push_back(UiRow{ "Master Volume", "1", "", false });
            (void)Ui->SetRows(screen, UiRowsIdAt(0), rows);
        };
        if (activated != nullptr)
            page.Activate = [activated](std::size_t row, const UiValue&) {
                activated->push_back("row" + std::to_string(row));
            };
        return page;
    }

    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
    UiSurfaceId Surface;
    RuntimeFrameLoop Runtime;
    InputContextSet Contexts;
    PauseState Pause;
    BackRouter Router;
    std::unique_ptr<PauseMenu> Menu;
};
}

TEST(PauseMenuStack, BackFromGameplayOpensTheRootAndPauses)
{
    ShellHost host;
    ASSERT_FALSE(host.Menu->IsOpen());

    EXPECT_TRUE(host.Menu->Back());
    EXPECT_EQ(host.Menu->Depth(), 1u);
    host.Frame();
    EXPECT_TRUE(host.Pause.IsPaused());
}

TEST(PauseMenuStack, BackAtTheRootResumes)
{
    ShellHost host;
    host.Menu->Open();
    host.Frame();
    ASSERT_TRUE(host.Pause.IsPaused());

    EXPECT_TRUE(host.Menu->Back());
    host.Frame();
    EXPECT_FALSE(host.Menu->IsOpen());
    EXPECT_FALSE(host.Pause.IsPaused());
}

TEST(PauseMenuStack, BackOnAPushedPagePopsToTheRootWithoutResuming)
{
    // The whole reason the stack exists: Escape in Options goes back to the
    // menu, not out to the game.
    ShellHost host;
    host.Menu->Open();
    host.Frame();
    host.Menu->Push(host.MakePage("options"));
    ASSERT_EQ(host.Menu->Depth(), 2u);

    EXPECT_TRUE(host.Menu->Back());
    EXPECT_EQ(host.Menu->Depth(), 1u);
    host.Frame();
    EXPECT_TRUE(host.Pause.IsPaused()) << "backing out of a page left the game running";
}

TEST(PauseMenuStack, AClosedShellTakesEveryPageWithIt)
{
    ShellHost host;
    host.Menu->Open();
    host.Frame();
    host.Menu->Push(host.MakePage("options"));
    ASSERT_EQ(host.Menu->Depth(), 2u);

    host.Menu->Close();
    host.Frame();
    EXPECT_EQ(host.Menu->Depth(), 0u);
    EXPECT_FALSE(host.Pause.IsPaused());
}

TEST(PauseMenuStack, APageOwnsWhatItsRowsMean)
{
    // The menu drains and routes; the page decides. A settings page's knowledge
    // of settings never reaches the menu.
    ShellHost host;
    std::vector<std::string> activated;
    host.Menu->Open();
    host.Frame();
    host.Menu->Push(host.MakePage("options", &activated));
    host.Frame();

    EXPECT_TRUE(activated.empty());
}

TEST(PauseMenuStack, APageCommitsOnceAsItCloses)
{
    // The commit boundary a settings store depends on: a value nudged a dozen
    // times is one write when the page goes, not a dozen while it is open.
    ShellHost host;
    int commits = 0;
    host.Menu->Open();
    host.Frame();

    PauseMenu::Page page = host.MakePage("options");
    page.Closed = [&commits] { ++commits; };
    host.Menu->Push(std::move(page));

    for (int frame = 0; frame < 5; ++frame)
        host.Frame();
    EXPECT_EQ(commits, 0) << "an open page committed while it was still open";

    host.Menu->Back();
    EXPECT_EQ(commits, 1);
    host.Frame();
    EXPECT_EQ(commits, 1) << "closing committed more than once";
}

TEST(PauseMenuStack, ClosingTheShellCommitsThePageUnderneathIt)
{
    // Resume with Options still open: the page has to commit on the way out,
    // or a setting the player just changed is lost to the one path that skips
    // popping it first.
    ShellHost host;
    int commits = 0;
    host.Menu->Open();
    host.Frame();

    PauseMenu::Page page = host.MakePage("options");
    page.Closed = [&commits] { ++commits; };
    host.Menu->Push(std::move(page));
    host.Frame();

    host.Menu->Close();
    EXPECT_EQ(commits, 1);
}

TEST(PauseMenuStack, TwoPagesCannotShareAModelName)
{
    // A document context holds one model per name. Worth pinning, because the
    // failure is a page that silently does not open.
    ShellHost host;
    host.Menu->Open();
    host.Frame();
    host.Menu->Push(host.MakePage("options"));
    host.Frame();
    ASSERT_EQ(host.Menu->Depth(), 2u);

    host.Menu->Push(host.MakePage("options"));
    host.Frame();
    // The stack still records it; what failed is the screen behind it.
    EXPECT_EQ(host.Menu->Depth(), 3u);

    // Backing out twice returns to the root either way, so a refused page
    // cannot strand the player.
    host.Menu->Back();
    host.Menu->Back();
    EXPECT_EQ(host.Menu->Depth(), 1u);
}
