// The proposal tray: open drafts and unopened proposal files listed, a draft
// picked to be shown, a file picked to be opened, and accept and reject acting
// on the draft shown through the same draft operations a client uses.

#include "WorkspaceFixture.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "notices/NoticeQueue.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"
#include "workspace/ProposalTray.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
class ProposalTrayTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Root = std::filesystem::temp_directory_path()
            / ("sencha_tray_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(Root);
        std::vector<std::string> errors;
        ASSERT_TRUE(Operations.Register("level", LevelOperations(Workspace.Level, Operations), errors));
        Tray.emplace(Workspace.Level, Operations, Notices, Root);
        Explorers.Offer(Catalog, Context, Tray->Row());
        Tray->Offer(Catalog, Context);
        Layer = Actions.Push(Context, ActionRank::Workspace);
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(Root, ignored);
    }

    void Ok(std::string_view name, std::string_view arguments)
    {
        const JsonValue answer = Operations.Invoke(name, JsonParse(arguments).value_or(JsonValue{}));
        ASSERT_EQ(answer.Find("status")->AsString(), "ok") << name << ": " << JsonStringify(answer);
    }

    std::vector<ExplorerItem> Listed()
    {
        return Explorers.Explore({ .Explorer = std::string(ProposalTray::kId), .First = 0, .Count = 50 }).Items;
    }

    bool Exists(std::string_view name)
    {
        return ResolveEntitySubject(Workspace.ActiveDocument(), "@" + std::string(name)).Resolved();
    }

    std::filesystem::path Root;
    EditorOperations Operations;
    NoticeQueue Notices;
    ActionCatalog Catalog;
    ActionContext Context;
    ExplorerTable Explorers;
    ActionContextStack Actions;
    ActionContextStack::Layer Layer;
    std::optional<ProposalTray> Tray;
};
}

TEST_F(ProposalTrayTest, ADraftIsListedWithWhatItChangesAndShownWhenPicked)
{
    Ok("draft.begin", R"({"label": "Hall"})");
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Ok("draft.begin", R"({"label": "Shed"})");

    const std::vector<ExplorerItem> items = Listed();
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].Label, "Hall · 1 new, 0 changed, 0 removed");
    EXPECT_FALSE(items[0].Current);
    EXPECT_TRUE(items[1].Current) << "the newest draft is the one shown";

    Explorers.Choose(ProposalTray::kId, items[0].Key, ExplorerChoice::Pick, 0);
    EXPECT_EQ(Workspace.Level.PreviewedDraft()->Label, "Hall");
}

TEST_F(ProposalTrayTest, AcceptAndRejectActOnTheDraftShown)
{
    Ok("draft.begin", R"({"label": "Hall"})");
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Ok("draft.begin", R"({"label": "Shed"})");
    Ok("space.create", R"({"name": "Shed", "min": [20, 0, 0], "max": [24, 3, 4]})");

    Actions.Invoke("proposal.reject");
    EXPECT_FALSE(Exists("Shed"));
    EXPECT_FALSE(Actions.Query("proposal.accept").Enabled) << "nothing is shown once the shown draft is gone";

    Explorers.Choose(ProposalTray::kId, Listed().front().Key, ExplorerChoice::Pick, 0);
    Actions.Invoke("proposal.accept");
    EXPECT_TRUE(Exists("Hall"));
    EXPECT_TRUE(Listed().empty());
    ASSERT_FALSE(Notices.Showing(NoticeClockNow()).empty());
    EXPECT_EQ(Notices.Showing(NoticeClockNow()).back().Text, "Accepted 'Hall'");
    Commands.Undo();
    EXPECT_FALSE(Exists("Hall")) << "an accepted proposal is one undo step";
}

TEST_F(ProposalTrayTest, AProposalFileIsListedUntilOpenedThenAsItsDraft)
{
    Ok("draft.begin", R"({"label": "Wing"})");
    Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    Ok("draft.save", R"({"path": ")" + (Root / "wing.sproposal").generic_string() + R"("})");
    Ok("draft.discard", "{}");

    std::vector<ExplorerItem> items = Listed();
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items.front().Label, "wing · not open");
    Explorers.Choose(ProposalTray::kId, items.front().Key, ExplorerChoice::Pick, 0);

    items = Listed();
    ASSERT_EQ(items.size(), 1u) << "the opened file is listed as its draft only";
    EXPECT_EQ(items.front().Label, "Wing · 1 new, 0 changed, 0 removed");
    EXPECT_TRUE(items.front().Current);
    EXPECT_EQ(Workspace.Level.WritingDraft(), nullptr) << "opened to review, not to write into";
}

