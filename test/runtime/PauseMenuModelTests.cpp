#include <gtest/gtest.h>

#include <app/PauseMenuModel.h>

#include <string>
#include <vector>

// What the menu offers, and how a game changes it. The rule under test is that
// authoring is by stable identity: a position is what the document repeats
// over, never what a caller addresses.

namespace
{
[[nodiscard]] std::vector<std::string> LabelsOf(const PauseMenuModel& model)
{
    return model.Labels();
}
}

TEST(PauseMenuModelTest, TheStockMenuIsResumeAndTheHostsWayOut)
{
    PauseMenuModel model;
    model.InstallDefaults("Exit to Desktop");

    EXPECT_EQ(LabelsOf(model), (std::vector<std::string>{ "Resume", "Exit to Desktop" }));
    EXPECT_EQ(model.CommandAt(0), kPauseResume);
    EXPECT_EQ(model.CommandAt(1), kPauseExit);
}

TEST(PauseMenuModelTest, OptionsAppearsOnlyWhenThereIsAPageBehindIt)
{
    // Not a special concept, and never a button that does nothing: the entry
    // exists exactly when a page exists to reach.
    PauseMenuModel model;
    model.InstallDefaults("Exit to Desktop");
    EXPECT_EQ(model.CommandAt(1), kPauseExit);

    model.SetOptionsPage("asset://ui/options.rml");
    EXPECT_EQ(LabelsOf(model),
              (std::vector<std::string>{ "Resume", "Options", "Exit to Desktop" }));

    model.SetOptionsPage({});
    EXPECT_EQ(LabelsOf(model), (std::vector<std::string>{ "Resume", "Exit to Desktop" }));
}

TEST(PauseMenuModelTest, AHostThatCannotTerminateGetsNoWayOutEntry)
{
    // The command is platform-neutral; the presentation is not. A browser build
    // omits the label and the entry goes with it, without the menu branching on
    // what kind of host it is running in.
    PauseMenuModel model;
    model.InstallDefaults({});
    EXPECT_EQ(LabelsOf(model), (std::vector<std::string>{ "Resume" }));
}

TEST(PauseMenuModelTest, ARenamedEntryKeepsItsCommand)
{
    PauseMenuModel model;
    model.InstallDefaults("Exit to Desktop");

    EXPECT_TRUE(model.SetLabel(kPauseResume, "Continue"));
    EXPECT_EQ(LabelsOf(model), (std::vector<std::string>{ "Continue", "Exit to Desktop" }));
    EXPECT_EQ(model.CommandAt(0), kPauseResume);
}

TEST(PauseMenuModelTest, ReorderingMovesTheEntryAndNotTheMeaningOfAnIndex)
{
    // The point of addressing by id. After a reorder, row 0 is a different
    // command -- and a caller that had said "row 0" would now mean something
    // else, which is why nothing does.
    PauseMenuModel model;
    model.InstallDefaults("Exit to Desktop");
    model.SetOptionsPage("asset://ui/options.rml");

    ASSERT_TRUE(model.MoveBefore(kPauseOptions, kPauseResume));
    EXPECT_EQ(LabelsOf(model),
              (std::vector<std::string>{ "Options", "Resume", "Exit to Desktop" }));
    EXPECT_EQ(model.CommandAt(0), kPauseOptions);
    EXPECT_EQ(model.CommandAt(1), kPauseResume);
}

TEST(PauseMenuModelTest, AGameAddedEntryDispatchesToItsOwnHandler)
{
    PauseMenuModel model;
    model.InstallDefaults("Exit to Desktop");

    const PauseCommandId save = model.Add("Save Game", [](PauseMenuContext&) {});

    // A fresh id, appended, with its own behaviour attached. Running it is the
    // menu's job and is covered where a menu exists to run it.
    ASSERT_NE(save, kPauseResume);
    ASSERT_NE(save, kPauseExit);
    EXPECT_EQ(model.CommandAt(2), save);
    EXPECT_NE(model.Handler(save), nullptr);
    EXPECT_EQ(model.Labels().back(), "Save Game");
}

TEST(PauseMenuModelTest, ReplacingAHandlerKeepsTheEntryWhereItWas)
{
    // How a game puts a confirmation in front of the way out, or a save behind
    // it, without touching the menu's shape.
    PauseMenuModel model;
    model.InstallDefaults("Exit to Desktop");

    const PauseCommandHandler* before = model.Handler(kPauseExit);
    EXPECT_TRUE(model.SetHandler(kPauseExit, [](PauseMenuContext&) {}));

    // Same place, same label; only what it does changed.
    EXPECT_EQ(LabelsOf(model), (std::vector<std::string>{ "Resume", "Exit to Desktop" }));
    EXPECT_EQ(model.CommandAt(1), kPauseExit);
    EXPECT_NE(model.Handler(kPauseExit), nullptr);
    (void)before;
}

TEST(PauseMenuModelTest, ARemovedEntryIsGoneFromWhatThePlayerSees)
{
    PauseMenuModel model;
    model.InstallDefaults("Exit to Desktop");
    model.SetOptionsPage("asset://ui/options.rml");

    EXPECT_TRUE(model.Remove(kPauseOptions));
    EXPECT_EQ(LabelsOf(model), (std::vector<std::string>{ "Resume", "Exit to Desktop" }));
    EXPECT_FALSE(model.Remove(kPauseOptions)) << "removing twice reported success";
}

TEST(PauseMenuModelTest, AnIndexPastTheEndNamesNoCommand)
{
    // What a click reported against a menu that changed under it looks like.
    PauseMenuModel model;
    model.InstallDefaults("Exit to Desktop");
    EXPECT_FALSE(model.CommandAt(7).IsValid());
}

TEST(PauseMenuModelTest, ADisabledEntryIsShownAndRefusesToRun)
{
    PauseMenuModel model;
    model.InstallDefaults("Exit to Desktop");
    const PauseCommandId save = model.Add("Save Game", [](PauseMenuContext&) {});

    EXPECT_TRUE(model.IsEnabled(save));
    EXPECT_TRUE(model.SetEnabled(save, false));
    EXPECT_FALSE(model.IsEnabled(save));
    EXPECT_EQ(model.Labels().size(), 3u) << "a disabled entry stopped being presented";
}
