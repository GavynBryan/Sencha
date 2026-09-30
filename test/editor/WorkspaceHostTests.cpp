#include "ui/WorkspaceView.h"
#include "workspaces/WorkspaceHost.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

// The workspaces an application has open: one per kind, built on first open
// and destroyed on close, one active, the one used before it taking over when
// the active one closes, and requests made mid-frame applied at the frame's
// boundary rather than where they were made.
namespace
{
using Log = std::vector<std::string>;

class FakeWorkspace final : public IWorkspace
{
public:
    FakeWorkspace(std::string kind, Log& log)
        : Kind(std::move(kind))
        , Events(log)
    {
        Events.push_back(Kind + " built");
    }
    ~FakeWorkspace() override { Events.push_back(Kind + " destroyed"); }

    void SetVisible(bool visible) override { Events.push_back(Kind + (visible ? " shown" : " hidden")); }
    WorkspaceView& View() override { return Surface; }

    std::string Kind;
    Log& Events;
    WorkspaceView Surface;
};

WorkspaceKind Kind(std::string id, Log& log, bool requiresProject = true)
{
    return WorkspaceKind{
        .Id = id,
        .DisplayName = id,
        .RequiresProject = requiresProject,
        .Create = [id, &log] { return std::make_unique<FakeWorkspace>(id, log); },
    };
}

constexpr PresentationId kMain{ 1, 1 };
constexpr PresentationId kSecond{ 2, 1 };
constexpr PresentationId kThird{ 3, 1 };

std::vector<WorkspaceKind> Table(Log& log)
{
    return { Kind("level", log), Kind("materials", log), Kind("project", log, /*requiresProject*/ false) };
}
}

TEST(WorkspaceHost, NothingIsBuiltUntilItIsOpened)
{
    Log log;
    WorkspaceHost host(Table(log), /*hasProject*/ true, kMain);
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(host.Active(), nullptr);

    ASSERT_NE(host.Open("level"), nullptr);
    EXPECT_EQ((Log{ "level built", "level shown" }), log);
    EXPECT_EQ(host.ActiveKind()->Id, "level");
}

TEST(WorkspaceHost, OpeningAnOpenKindActivatesTheSameInstance)
{
    Log log;
    WorkspaceHost host(Table(log), true, kMain);
    IWorkspace* level = host.Open("level");
    (void)host.Open("materials");
    log.clear();

    EXPECT_EQ(host.Open("level"), level);
    EXPECT_EQ((Log{ "materials hidden", "level shown" }), log);
    EXPECT_EQ(host.OpenWorkspaces().size(), 2u);
}

TEST(WorkspaceHost, ClosingTheActiveOneFallsBackToTheOneUsedBeforeIt)
{
    Log log;
    WorkspaceHost host(Table(log), true, kMain);
    (void)host.Open("level");
    (void)host.Open("project");
    (void)host.Open("materials");
    (void)host.Activate("level");
    (void)host.Activate("materials");
    log.clear();

    EXPECT_TRUE(host.Close("materials"));
    EXPECT_EQ((Log{ "materials destroyed", "level shown" }), log)
        << "the fallback is the most recently active, not the neighbouring tab";
    EXPECT_EQ(host.ActiveKind()->Id, "level");
}

TEST(WorkspaceHost, ClosingABackgroundOneLeavesTheActiveOneAlone)
{
    Log log;
    WorkspaceHost host(Table(log), true, kMain);
    (void)host.Open("level");
    (void)host.Open("materials");
    log.clear();

    EXPECT_TRUE(host.Close("level"));
    EXPECT_EQ((Log{ "level destroyed" }), log);
    EXPECT_EQ(host.ActiveKind()->Id, "materials");
}

TEST(WorkspaceHost, AKindNeedingAProjectIsNotOfferedWithoutOne)
{
    Log log;
    WorkspaceHost host(Table(log), /*hasProject*/ false, kMain);
    EXPECT_FALSE(host.IsOffered("level"));
    EXPECT_EQ(host.Open("level"), nullptr);
    EXPECT_TRUE(log.empty());
    EXPECT_NE(host.Open("project"), nullptr);
    EXPECT_FALSE(host.IsOffered("unknown"));
}

TEST(WorkspaceHost, TheListenersSeeAWorkspaceWhileItExists)
{
    Log log;
    WorkspaceHost host(Table(log), true, kMain);
    host.SetOpenedListener([&](const WorkspaceKind& kind, IWorkspace&) { log.push_back("opened " + kind.Id); });
    host.SetClosingListener([&](const WorkspaceKind& kind, IWorkspace&) { log.push_back("closing " + kind.Id); });

    (void)host.Open("level");
    (void)host.Close("level");
    EXPECT_EQ((Log{ "level built", "opened level", "level shown", "closing level", "level destroyed" }), log);
}

TEST(WorkspaceHost, RequestsWaitForTheFrameBoundary)
{
    Log log;
    WorkspaceHost host(Table(log), true, kMain);
    (void)host.Open("level");
    log.clear();

    host.Request({ WorkspaceAction::Open, "materials" });
    host.Request({ WorkspaceAction::Close, "level" });
    EXPECT_TRUE(log.empty()) << "a request made mid-frame changed nothing yet";

    host.ApplyRequests();
    EXPECT_EQ((Log{ "materials built", "level hidden", "materials shown", "level destroyed" }), log);
    EXPECT_EQ(host.OpenWorkspaces().size(), 1u);
}

TEST(WorkspaceHost, DestroyingTheHostClosesEverything)
{
    Log log;
    {
        WorkspaceHost host(Table(log), true, kMain);
        (void)host.Open("level");
        (void)host.Open("materials");
        log.clear();
    }
    EXPECT_EQ((Log{ "materials destroyed", "level destroyed" }), log);
}

TEST(WorkspaceHost, AGuardHoldsACloseAndTheWayOutPassesIt)
{
    Log log;
    WorkspaceHost host(Table(log), true, kMain);
    bool mayClose = false;
    host.SetCloseGuard([&](const WorkspaceKind&, IWorkspace&) { return mayClose; });
    (void)host.Open("level");
    log.clear();

    EXPECT_FALSE(host.Close("level"));
    EXPECT_NE(host.Find("level"), nullptr) << "a held close destroyed the workspace";
    mayClose = true;
    EXPECT_TRUE(host.Close("level"));
    EXPECT_EQ((Log{ "level destroyed" }), log);

    mayClose = false;
    (void)host.Open("materials");
    host.CloseAll();
    EXPECT_TRUE(host.OpenWorkspaces().empty()) << "the application's exit is past the guard";
}

TEST(WorkspaceHost, EachWindowShowsItsOwnMostRecentWorkspace)
{
    Log log;
    WorkspaceHost host(Table(log), true, kMain);
    (void)host.Open("level");
    (void)host.Open("materials");
    host.SetWindowOpener([](const WorkspaceKind&) { return kSecond; });
    log.clear();

    ASSERT_TRUE(host.Detach("materials"));
    EXPECT_EQ((Log{ "materials hidden", "materials shown", "level shown" }), log)
        << "the move interrupts it, it shows in its new window, and the main one falls back";
    EXPECT_EQ(host.WindowOf("materials"), kSecond);
    EXPECT_EQ(host.ActiveKindIn(kMain)->Id, "level");
    EXPECT_EQ(host.ActiveKindIn(kSecond)->Id, "materials");
    EXPECT_EQ(host.ActiveKind()->Id, "materials") << "the one used last, in whichever window";

    log.clear();
    (void)host.Activate("level");
    EXPECT_TRUE(log.empty()) << "activating in one window hides nothing in another";
    EXPECT_EQ(host.ActiveKind()->Id, "level");
}

TEST(WorkspaceHost, ANewWorkspaceOpensInTheMainWindow)
{
    Log log;
    WorkspaceHost host(Table(log), true, kMain);
    (void)host.Open("level");
    ASSERT_TRUE(host.Place("level", kSecond));
    (void)host.Open("materials");
    EXPECT_EQ(host.WindowOf("materials"), kMain);
    EXPECT_EQ(host.ActiveKindIn(kSecond)->Id, "level");
}

TEST(WorkspaceHost, ClosingAWindowBringsItsWorkspacesBack)
{
    Log log;
    WorkspaceHost host(Table(log), true, kMain);
    std::vector<PresentationId> emptied;
    host.SetWindowEmptiedListener([&](PresentationId window) { emptied.push_back(window); });
    (void)host.Open("level");
    (void)host.Open("materials");
    (void)host.Open("project");
    ASSERT_TRUE(host.Place("materials", kSecond));
    ASSERT_TRUE(host.Place("project", kSecond));
    EXPECT_TRUE(emptied.empty());

    host.Gather(kSecond);
    EXPECT_EQ(host.WindowOf("materials"), kMain);
    EXPECT_EQ(host.WindowOf("project"), kMain);
    EXPECT_EQ(emptied, (std::vector<PresentationId>{ kSecond })) << "reported once, when the last one left";
    EXPECT_EQ(host.ActiveIn(kSecond), nullptr);

    emptied.clear();
    host.Gather(kThird);
    EXPECT_EQ(emptied, (std::vector<PresentationId>{ kThird })) << "an empty window closing is still reported";
}

TEST(WorkspaceHost, ClosingTheLastWorkspaceInAWindowEmptiesIt)
{
    Log log;
    WorkspaceHost host(Table(log), true, kMain);
    std::vector<PresentationId> emptied;
    host.SetWindowEmptiedListener([&](PresentationId window) { emptied.push_back(window); });
    (void)host.Open("level");
    (void)host.Open("materials");
    ASSERT_TRUE(host.Place("materials", kSecond));
    ASSERT_TRUE(host.Close("materials"));
    EXPECT_EQ(emptied, (std::vector<PresentationId>{ kSecond }));

    ASSERT_TRUE(host.Close("level"));
    EXPECT_EQ(emptied.size(), 1u) << "the main window stays, empty or not";
}

TEST(WorkspaceHost, APlacedWorkspaceIsReboundBeforeItShows)
{
    Log log;
    WorkspaceHost host(Table(log), true, kMain);
    host.SetPlacedListener([&](const WorkspaceKind& kind, IWorkspace&) { log.push_back("placed " + kind.Id); });
    (void)host.Open("level");
    log.clear();
    host.Request({ WorkspaceAction::Detach, "level" });
    host.ApplyRequests();
    EXPECT_TRUE(log.empty()) << "detaching needs a window, and nothing made one";

    host.SetWindowOpener([](const WorkspaceKind&) { return kSecond; });
    host.Request({ WorkspaceAction::Detach, "level" });
    host.Request({ WorkspaceAction::Attach, "level" });
    host.ApplyRequests();
    EXPECT_EQ((Log{ "level hidden", "placed level", "level shown", "level hidden", "placed level", "level shown" }),
              log);
    EXPECT_EQ(host.WindowOf("level"), kMain);
}
