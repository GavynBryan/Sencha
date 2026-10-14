#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// The catalog is what every way of reaching an operation reads -- a chord, a
// menu, the console, a palette -- and the context stack is what decides who
// performs it in the window in use. These pin the two apart: definitions are
// data, and handlers live exactly as long as the context that offers them.

namespace
{
constexpr KeyChord kCtrlZ{ SDLK_Z, { .Ctrl = true } };
constexpr KeyChord kDelete{ SDLK_DELETE, {} };

ActionDefinition Define(std::string id, std::vector<KeyChord> chords = {})
{
    return ActionDefinition{ .Id = std::move(id), .Label = "Label", .Category = "Edit",
                             .DefaultChords = std::move(chords) };
}
} // namespace

TEST(ActionCatalog, RedefiningIdenticallyIsAWorkspaceReopeningNotAConflict)
{
    ActionCatalog catalog;
    ASSERT_TRUE(catalog.Define(Define("edit.undo", { kCtrlZ })));
    EXPECT_TRUE(catalog.Define(Define("edit.undo", { kCtrlZ })));

    std::string error;
    EXPECT_FALSE(catalog.Define(Define("edit.undo", { kDelete }), &error))
        << "two different operations cannot share an id";
    EXPECT_FALSE(error.empty());
}

TEST(ActionCatalog, AnActionNamedForDataFollowsItsRename)
{
    ActionCatalog catalog;
    ASSERT_TRUE(catalog.Define(Define("transport.profile.nightly", { kDelete })));
    EXPECT_TRUE(catalog.Relabel("transport.profile.nightly", "Cook with Nightly Build"));
    EXPECT_EQ(catalog.Find("transport.profile.nightly")->Label, "Cook with Nightly Build");
    ASSERT_EQ(catalog.ForChord(kDelete).size(), 1u);
    EXPECT_EQ(catalog.ForChord(kDelete)[0]->Id, "transport.profile.nightly") << "the chord stays with the action";
    EXPECT_FALSE(catalog.Relabel("transport.profile.gone", "Cook with Nothing"));
}

TEST(ActionCatalog, AChordNamesItsActionAndReadsBackAsText)
{
    ActionCatalog catalog;
    ASSERT_TRUE(catalog.Define(Define("edit.undo", { kCtrlZ })));
    const std::vector<const ActionDefinition*> found = catalog.ForChord(kCtrlZ);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0]->Id, "edit.undo");
    EXPECT_EQ(catalog.ChordText("edit.undo"), "Ctrl+Z");
    EXPECT_TRUE(catalog.ForChord(kDelete).empty());
}

TEST(ActionCatalog, TheKeymapRebindsActionsDefinedBeforeAndAfterItIsRead)
{
    // A workspace opened later defines its actions after the keymap was read;
    // the user's chord still has to win.
    ActionCatalog catalog;
    ASSERT_TRUE(catalog.Define(Define("edit.undo", { kCtrlZ })));
    const KeyChord ctrlU{ SDLK_U, { .Ctrl = true } };
    const std::vector<std::string> unknown =
        catalog.ApplyKeymap({ { "edit.undo", ctrlU }, { "tool.brush", KeyChord{ SDLK_N, {} } } });
    EXPECT_EQ(unknown, std::vector<std::string>{ "tool.brush" });
    EXPECT_EQ(catalog.ChordText("edit.undo"), "Ctrl+U");
    EXPECT_TRUE(catalog.ForChord(kCtrlZ).empty()) << "the default chord outlived its rebinding";

    ASSERT_TRUE(catalog.Define(Define("tool.brush", { KeyChord{ SDLK_B, {} } })));
    EXPECT_EQ(catalog.ChordText("tool.brush"), "N");
}

TEST(ActionCatalog, TwoActionsAnsweringOneChordAreReported)
{
    ActionCatalog catalog;
    ASSERT_TRUE(catalog.Define(Define("edit.delete", { kDelete })));
    ASSERT_TRUE(catalog.Define(Define("asset.delete", { kDelete })));
    const std::vector<ActionChordConflict> conflicts = catalog.Conflicts();
    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts[0].First, "edit.delete");
    EXPECT_EQ(conflicts[0].Second, "asset.delete");
}

TEST(ActionCatalog, AHostHearsOfEachActionOnceWhenItIsFirstDefined)
{
    ActionCatalog catalog;
    std::vector<std::string> heard;
    catalog.SetDefinedObserver([&](const ActionDefinition& action) { heard.push_back(action.Id); });
    ASSERT_TRUE(catalog.Define(Define("edit.undo")));
    ASSERT_TRUE(catalog.Define(Define("edit.undo")));
    ASSERT_TRUE(catalog.Define(Define("edit.redo")));
    EXPECT_EQ(heard, (std::vector<std::string>{ "edit.undo", "edit.redo" }));
}

TEST(ActionContextStack, TheContextClosestToThePersonAnswers)
{
    // Delete deletes the asset in a focused explorer, the selection in the
    // workspace, and nothing the application has an opinion about.
    ActionContextStack stack;
    ActionContext application;
    ActionContext workspace;
    ActionContext explorer;
    std::string performed;
    workspace.Claim("edit.delete", { .Invoke = [&] { performed = "selection"; } });
    explorer.Claim("edit.delete", { .Invoke = [&] { performed = "asset"; } });

    const auto explorerLayer = stack.Push(explorer, ActionRank::Surface);
    const auto applicationLayer = stack.Push(application, ActionRank::Application);
    const auto workspaceLayer = stack.Push(workspace, ActionRank::Workspace);

    EXPECT_TRUE(stack.Invoke("edit.delete"));
    EXPECT_EQ(performed, "asset") << "pushed first, but a focused surface outranks the workspace";
}

TEST(ActionContextStack, AContextOutOfPlayTakesItsHandlersWithIt)
{
    ActionContextStack stack;
    ActionContext workspace;
    ActionContext explorer;
    std::string performed;
    workspace.Claim("edit.delete", { .Invoke = [&] { performed = "selection"; } });
    explorer.Claim("edit.delete", { .Invoke = [&] { performed = "asset"; } });
    const auto workspaceLayer = stack.Push(workspace, ActionRank::Workspace);
    {
        const auto explorerLayer = stack.Push(explorer, ActionRank::Surface);
        EXPECT_TRUE(stack.Invoke("edit.delete"));
        EXPECT_EQ(performed, "asset");
    }
    EXPECT_TRUE(stack.Invoke("edit.delete"));
    EXPECT_EQ(performed, "selection") << "the closed explorer still answered";
}

TEST(ActionContextStack, WithinOneRankTheNewestAnswers)
{
    ActionContextStack stack;
    ActionContext older;
    ActionContext newer;
    std::string performed;
    older.Claim("file.save", { .Invoke = [&] { performed = "older"; } });
    newer.Claim("file.save", { .Invoke = [&] { performed = "newer"; } });
    const auto a = stack.Push(older, ActionRank::Document);
    const auto b = stack.Push(newer, ActionRank::Document);
    EXPECT_TRUE(stack.Invoke("file.save"));
    EXPECT_EQ(performed, "newer");
}

TEST(ActionContextStack, AChordSharedByActionsNeverClaimedTogetherReachesTheOneInPlay)
{
    // New Level was defined first, but in the materials workspace Ctrl+N is
    // New Material.
    ActionCatalog catalog;
    const KeyChord ctrlN{ SDLK_N, { .Ctrl = true } };
    ASSERT_TRUE(catalog.Define(Define("file.new", { ctrlN })));
    ASSERT_TRUE(catalog.Define(Define("material.new", { ctrlN })));
    ActionContextStack stack;
    ActionContext application;
    ActionContext materials;
    std::string performed;
    application.Claim("file.close", { .Invoke = [&] { performed = "close"; } });
    materials.Claim("material.new", { .Invoke = [&] { performed = "material"; } });
    const auto applicationLayer = stack.Push(application, ActionRank::Application);
    const auto materialsLayer = stack.Push(materials, ActionRank::Workspace);
    EXPECT_TRUE(stack.Dispatch(catalog, ctrlN));
    EXPECT_EQ(performed, "material");

    ActionContext surface;
    surface.Claim("file.new", { .Invoke = [&] { performed = "file"; } });
    const auto surfaceLayer = stack.Push(surface, ActionRank::Surface);
    EXPECT_TRUE(stack.Dispatch(catalog, ctrlN));
    EXPECT_EQ(performed, "file") << "the closer claim answers, whichever action it is";
}

TEST(ActionContextStack, ADisabledActionIsClaimedButNotPerformed)
{
    // Ctrl+Z with nothing to undo belongs to the editor all the same: it must
    // not fall through to whatever else might read that key.
    ActionContextStack stack;
    ActionContext application;
    int performed = 0;
    application.Claim("edit.undo", { .Invoke = [&] { ++performed; }, .Enabled = [] { return false; } });
    const auto layer = stack.Push(application, ActionRank::Application);

    ActionCatalog catalog;
    ASSERT_TRUE(catalog.Define(Define("edit.undo", { kCtrlZ })));
    EXPECT_TRUE(stack.Dispatch(catalog, kCtrlZ));
    EXPECT_EQ(performed, 0);
    const ActionContextStack::State state = stack.Query("edit.undo");
    EXPECT_TRUE(state.Claimed);
    EXPECT_FALSE(state.Enabled);

    EXPECT_FALSE(stack.Dispatch(catalog, kDelete)) << "a chord no action answers is not the window's";
}

TEST(ActionContextStack, AClaimThatCannotActNowPassesTheChordOutward)
{
    // The shelf holds the keys with no file chosen: its Delete cannot act, so
    // Delete deletes what the workspace has selected instead of nothing.
    ActionCatalog catalog;
    ASSERT_TRUE(catalog.Define(Define("edit.delete", { kDelete })));
    ActionContextStack stack;
    ActionContext workspace;
    ActionContext shelf;
    std::string performed;
    bool fileChosen = false;
    workspace.Claim("edit.delete", { .Invoke = [&] { performed = "selection"; } });
    shelf.Claim("edit.delete", { .Invoke = [&] { performed = "file"; }, .Enabled = [&] { return fileChosen; } });
    const auto workspaceLayer = stack.Push(workspace, ActionRank::Workspace);
    const auto shelfLayer = stack.Push(shelf, ActionRank::Surface);

    EXPECT_TRUE(stack.Dispatch(catalog, kDelete));
    EXPECT_EQ(performed, "selection");
    EXPECT_FALSE(stack.Query("edit.delete").Enabled) << "what is shown is still the nearest claim's";

    fileChosen = true;
    EXPECT_TRUE(stack.Dispatch(catalog, kDelete));
    EXPECT_EQ(performed, "file");
}

TEST(ActionContextStack, AMovedLayerStaysInPlay)
{
    ActionContextStack stack;
    ActionContext context;
    int performed = 0;
    context.Claim("edit.undo", { .Invoke = [&] { ++performed; } });
    ActionContextStack::Layer kept;
    {
        ActionContextStack::Layer pushed = stack.Push(context, ActionRank::Workspace);
        kept = std::move(pushed);
    }
    EXPECT_TRUE(stack.Invoke("edit.undo"));
    EXPECT_EQ(performed, 1);
    kept.Reset();
    EXPECT_FALSE(stack.Invoke("edit.undo"));
}
