#include <gtest/gtest.h>

#include <app/EngineConsoleBuiltins.h>
#include <app/OptionsPage.h>
#include <audio/AudioService.h>
#include <core/config/AudioConfig.h>
#include <core/console/ConsoleRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <ui/UiValue.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

// What a player is offered, and what activating a row does. The rule under test
// is that the page is a curated table over settings this host can actually
// apply -- not a listing of every archived cvar, and not a set of rows that
// refuse when pressed.

namespace
{
EngineAudioConfig DummyAudio()
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    EngineAudioConfig config;
    config.EnablePlayback = true;
    return config;
}

[[nodiscard]] std::vector<std::string> LabelsOf(const OptionsPage& page,
                                                const ConsoleRegistry& registry)
{
    std::vector<std::string> labels;
    for (const UiRow& row : page.Present(registry))
        labels.push_back(row.Label);
    return labels;
}

OptionRow ToggleRow(std::string label)
{
    OptionRow row;
    row.Label = std::move(label);
    row.Control = OptionControl::Toggle;
    return row;
}

void RegisterReducedFlashing(ConsoleRegistry& registry)
{
    ASSERT_TRUE(registry.RegisterCVar({
        .Name = "render.reduced_flashing",
        .Type = CVarType::Bool,
        .DefaultValue = false,
        .CurrentValue = false,
    }));
}
}

TEST(OptionsPageTest, AHostWithNoSettingsOffersNoRows)
{
    // Which is why the pause menu's Options entry is absent rather than opening
    // an empty page: the entry follows the page, and the page follows the host.
    ConsoleRegistry registry;
    OptionsPage page;
    page.InstallDefaults(registry);
    EXPECT_TRUE(page.Rows().empty());
}

TEST(OptionsPageTest, OnlyTheSettingsThisHostRegisteredGetRows)
{
    LoggingProvider logging;
    AudioService audio(logging, DummyAudio());
    World world;
    ConsoleRegistry registry;
    EngineConsoleBuiltins::RegisterPlayerSettingCVars(registry, &audio, nullptr, &world);

    OptionsPage page;
    page.InstallDefaults(registry);

    // Volume and sensitivity registered; no window, so no display mode, and no
    // frame cap because nothing registered r.target_fps here.
    EXPECT_EQ(LabelsOf(page, registry),
              (std::vector<std::string>{ "Master Volume", "Look Sensitivity", "Reduced Flashing" }));
}

TEST(OptionsPageTest, ThePageIsCuratedRatherThanEveryArchivedCVar)
{
    // The ~12 cvars carrying Archive today are engine tuning. A page over all of
    // them would be a developer panel wearing a player's name.
    ConsoleRegistry registry;
    ASSERT_TRUE(registry.RegisterCVar({
        .Name = "time.cadence_lock_tolerance",
        .Owner = "engine",
        .Type = CVarType::Double,
        .DefaultValue = 0.25,
        .CurrentValue = 0.25,
        .Flags = CVarFlags::Archive,
    }));

    OptionsPage page;
    page.InstallDefaults(registry);
    EXPECT_TRUE(page.Rows().empty())
        << "an engine tuning knob was offered to a player as a setting";
}

TEST(OptionsPageTest, ApplyingARangeWritesTheValueSnappedAndClamped)
{
    LoggingProvider logging;
    AudioService audio(logging, DummyAudio());
    ConsoleRegistry registry;
    EngineConsoleBuiltins::RegisterPlayerSettingCVars(registry, &audio, nullptr, nullptr);

    OptionsPage page;
    page.InstallDefaults(registry);
    ASSERT_EQ(page.Rows().size(), 1u);

    // The slider reports a float; the row snaps it to its step.
    EXPECT_TRUE(page.Apply(registry, 0, UiValue(0.37)));
    EXPECT_NEAR(audio.GetMasterVolume(), 0.35f, 1e-5f);
    // And clamps it to its bounds.
    EXPECT_TRUE(page.Apply(registry, 0, UiValue(5.0)));
    EXPECT_FLOAT_EQ(audio.GetMasterVolume(), 1.0f);
    // A string is not what a slider reports, whatever it says: it is the row's
    // hidden drop-down echoing on publish, and is refused as such.
    EXPECT_FALSE(page.Apply(registry, 0, UiValue(std::string("loud"))));
    EXPECT_FALSE(page.Apply(registry, 0, UiValue(std::string("0.5"))));
    EXPECT_FLOAT_EQ(audio.GetMasterVolume(), 1.0f);
}

TEST(OptionsPageTest, ApplyingAChoiceLabelWritesTheValueItStandsFor)
{
    ConsoleRegistry registry;
    ASSERT_TRUE(registry.RegisterCVar({
        .Name = "window.mode",
        .Owner = "engine",
        .Type = CVarType::String,
        .DefaultValue = std::string("windowed"),
        .CurrentValue = std::string("windowed"),
        .Flags = CVarFlags::Archive,
    }));

    OptionsPage page;
    page.InstallDefaults(registry);
    ASSERT_EQ(page.Rows().size(), 1u);

    EXPECT_TRUE(page.Apply(registry, 0, UiValue(std::string("Fullscreen"))));
    EXPECT_EQ(std::get<std::string>(registry.FindCVar("window.mode")->CurrentValue), "fullscreen");
    // The document only ever sees labels; a value is not one.
    EXPECT_FALSE(page.Apply(registry, 0, UiValue(std::string("borderless"))));
    EXPECT_FALSE(page.Apply(registry, 0, UiValue(std::string("Sideways"))));
    EXPECT_EQ(std::get<std::string>(registry.FindCVar("window.mode")->CurrentValue), "fullscreen");
}

TEST(OptionsPageTest, AToggleReadsAndWritesABooleanCVar)
{
    ConsoleRegistry registry;
    ASSERT_TRUE(registry.RegisterCVar({
        .Name = "render.reduced_flashing",
        .Owner = "engine",
        .Type = CVarType::Bool,
        .DefaultValue = false,
        .CurrentValue = false,
        .Flags = CVarFlags::Archive,
    }));

    OptionsPage page;
    page.InstallDefaults(registry);
    ASSERT_EQ(page.Rows().size(), 1u);
    EXPECT_EQ(page.Rows()[0].Control, OptionControl::Toggle);
    UiRow row = page.Present(registry)[0];
    EXPECT_TRUE(row.Editable);
    EXPECT_EQ(row.Control, UiRowControls::Toggle);
    EXPECT_EQ(row.Choices, (std::vector<std::string>{ "Off", "On" }));
    EXPECT_EQ(row.Value, "Off");

    const std::uint64_t before = registry.ArchiveRevision();
    EXPECT_FALSE(page.Apply(registry, 0, UiValue(std::string("Off")))) << "opening the page wrote";
    EXPECT_EQ(registry.ArchiveRevision(), before);

    EXPECT_TRUE(page.Apply(registry, 0, UiValue(std::string("On"))));
    EXPECT_TRUE(std::get<bool>(registry.FindCVar("render.reduced_flashing")->CurrentValue));
    EXPECT_EQ(page.Present(registry)[0].Value, "On");
    EXPECT_FALSE(page.Apply(registry, 0, UiValue(std::string("On")))) << "already on";

    ASSERT_TRUE(registry.SetCVarFromString("render.reduced_flashing", "off", { "console" },
                                           ConsolePhase::EngineReady).Succeeded());
    EXPECT_EQ(page.Present(registry)[0].Value, "Off");

    EXPECT_FALSE(page.Apply(registry, 0, UiValue(std::string("Maybe"))));
    EXPECT_FALSE(page.Apply(registry, 0, UiValue(true))) << "a toggle reports its label";
    EXPECT_FALSE(std::get<bool>(registry.FindCVar("render.reduced_flashing")->CurrentValue));
}

TEST(OptionsPageTest, ARowWhoseCVarDoesNotSuitItsControlIsShownAndNotOffered)
{
    ConsoleRegistry registry;
    ASSERT_TRUE(registry.RegisterCVar({
        .Name = "game.hints",
        .Owner = "game",
        .Type = CVarType::String,
        .DefaultValue = std::string("on"),
        .CurrentValue = std::string("on"),
        .Flags = CVarFlags::Archive,
    }));
    ASSERT_TRUE(registry.RegisterCVar({
        .Name = "game.flag",
        .Owner = "game",
        .Type = CVarType::Bool,
        .DefaultValue = false,
        .CurrentValue = false,
        .Flags = CVarFlags::Archive,
    }));

    OptionsPage page;
    page.RegisterRows("game", {
        OptionRow{ .Label = "Hints", .CVar = "game.hints", .Control = OptionControl::Toggle,
                   .Step = 0.0, .Min = 0.0, .Max = 0.0, .Choices = {} },
        OptionRow{ .Label = "Flag", .CVar = "game.flag", .Control = OptionControl::Range,
                   .Step = 0.1, .Min = 0.0, .Max = 1.0, .Choices = {} },
        OptionRow{ .Label = "Gone", .CVar = "game.gone", .Control = OptionControl::Toggle,
                   .Step = 0.0, .Min = 0.0, .Max = 0.0, .Choices = {} },
    });

    const std::vector<UiRow> rows = page.Present(registry);
    ASSERT_EQ(rows.size(), 3u);
    for (const UiRow& row : rows)
    {
        EXPECT_FALSE(row.Editable) << row.Label;
        EXPECT_EQ(row.Control, UiRowControls::Text) << row.Label;
    }

    const std::uint64_t before = registry.ArchiveRevision();
    EXPECT_FALSE(page.Apply(registry, 0, UiValue(std::string("Off"))));
    EXPECT_FALSE(page.Apply(registry, 1, UiValue(1.0)));
    EXPECT_FALSE(page.Apply(registry, 2, UiValue(std::string("On"))));
    EXPECT_EQ(registry.ArchiveRevision(), before);
    EXPECT_EQ(std::get<std::string>(registry.FindCVar("game.hints")->CurrentValue), "on");
}

TEST(OptionsPageTest, WhichControlsSuitWhichCVars)
{
    EXPECT_TRUE(OptionControlSuits(OptionControl::Range, CVarType::Double));
    EXPECT_TRUE(OptionControlSuits(OptionControl::Range, CVarType::Int));
    EXPECT_FALSE(OptionControlSuits(OptionControl::Range, CVarType::Bool));
    EXPECT_FALSE(OptionControlSuits(OptionControl::Range, CVarType::String));
    EXPECT_TRUE(OptionControlSuits(OptionControl::Toggle, CVarType::Bool));
    EXPECT_FALSE(OptionControlSuits(OptionControl::Toggle, CVarType::String));
    EXPECT_FALSE(OptionControlSuits(OptionControl::Toggle, CVarType::Int));
    for (const CVarType type : { CVarType::Bool, CVarType::Int, CVarType::Double, CVarType::String })
        EXPECT_TRUE(OptionControlSuits(OptionControl::Choice, type));
}

TEST(OptionsPageTest, AFrameCapLabelMapsToTheNumberBehindIt)
{
    ConsoleRegistry registry;
    ASSERT_TRUE(registry.RegisterCVar({
        .Name = "r.target_fps",
        .Owner = "engine",
        .Type = CVarType::Double,
        .DefaultValue = 0.0,
        .CurrentValue = 60.0,
        .Flags = CVarFlags::Archive,
    }));

    OptionsPage page;
    page.InstallDefaults(registry);
    ASSERT_EQ(page.Rows().size(), 1u);

    EXPECT_TRUE(page.Apply(registry, 0, UiValue(std::string("144"))));
    EXPECT_DOUBLE_EQ(std::get<double>(registry.FindCVar("r.target_fps")->CurrentValue), 144.0);
    EXPECT_TRUE(page.Apply(registry, 0, UiValue(std::string("Unlimited"))));
    EXPECT_DOUBLE_EQ(std::get<double>(registry.FindCVar("r.target_fps")->CurrentValue), 0.0);
}

TEST(OptionsPageTest, PublishingThePageIsAFixedPoint)
{
    // The document engine raises a change for every control as the page opens,
    // carrying the value it was just given. Applying that value must write
    // nothing -- including when the stored value is not on the slider's step,
    // which is exactly what a file written by an earlier build can hold.
    LoggingProvider logging;
    AudioService audio(logging, DummyAudio());
    ConsoleRegistry registry;
    EngineConsoleBuiltins::RegisterPlayerSettingCVars(registry, &audio, nullptr, nullptr);
    ASSERT_TRUE(registry.SetCVar("audio.volume", CVarValue{ 0.9999999999999999 }, { "file" },
                                 ConsolePhase::EngineReady).Succeeded());
    const std::uint64_t before = registry.ArchiveRevision();

    OptionsPage page;
    page.InstallDefaults(registry);
    const std::vector<UiRow> rows = page.Present(registry);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_DOUBLE_EQ(rows.front().Number, 1.0);
    EXPECT_EQ(rows.front().Value, "1");

    // What the slider reports back is what it shows.
    EXPECT_FALSE(page.Apply(registry, 0, UiValue(1.0)));
    EXPECT_EQ(registry.ArchiveRevision(), before) << "opening the page changed a setting";
}

TEST(OptionsPageTest, AChoiceNobodyOfferedIsPreservedRatherThanReplaced)
{
    ConsoleRegistry registry;
    ASSERT_TRUE(registry.RegisterCVar({
        .Name = "r.target_fps",
        .Owner = "engine",
        .Type = CVarType::Double,
        .DefaultValue = 0.0,
        .CurrentValue = 165.0,
        .Flags = CVarFlags::Archive,
    }));

    OptionsPage page;
    page.InstallDefaults(registry);
    std::vector<UiRow> rows = page.Present(registry);
    ASSERT_EQ(rows.size(), 1u);
    // Shown as itself, and offered as an option so the drop-down can select it
    // instead of falling back to the first entry.
    EXPECT_EQ(rows.front().Value, "165");
    EXPECT_EQ(rows.front().Choices.back(), "165");

    // The open-time change carries that same label: a no-op, not a write.
    const std::uint64_t before = registry.ArchiveRevision();
    EXPECT_FALSE(page.Apply(registry, 0, UiValue(std::string("165"))));
    EXPECT_EQ(registry.ArchiveRevision(), before);
    EXPECT_DOUBLE_EQ(std::get<double>(registry.FindCVar("r.target_fps")->CurrentValue), 165.0);

    // Picking a listed cap still works from there.
    EXPECT_TRUE(page.Apply(registry, 0, UiValue(std::string("120"))));
    EXPECT_DOUBLE_EQ(std::get<double>(registry.FindCVar("r.target_fps")->CurrentValue), 120.0);
    rows = page.Present(registry);
    EXPECT_EQ(rows.front().Value, "120");
    EXPECT_EQ(rows.front().Choices.size(), 7u) << "the raw entry is gone once a known one holds";
}

TEST(OptionsPageTest, WhatThePageShowsIsWhatTheCVarSays)
{
    // Read through, not cached: the console can change a setting while the page
    // is open, and the page must not go on presenting what it was.
    LoggingProvider logging;
    AudioService audio(logging, DummyAudio());
    ConsoleRegistry registry;
    EngineConsoleBuiltins::RegisterPlayerSettingCVars(registry, &audio, nullptr, nullptr);

    OptionsPage page;
    page.InstallDefaults(registry);

    ASSERT_TRUE(registry.SetCVar("audio.volume", CVarValue{ 0.5 }, { "console" },
                                 ConsolePhase::EngineReady).Succeeded());
    const std::vector<UiRow> rows = page.Present(registry);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows.front().Control, UiRowControls::Range);
    EXPECT_DOUBLE_EQ(rows.front().Number, 0.5);
    EXPECT_EQ(rows.front().Value, "0.5");
}

TEST(OptionsPageTest, AnUncappedFrameRateReadsAsUnlimitedRatherThanZero)
{
    ConsoleRegistry registry;
    ASSERT_TRUE(registry.RegisterCVar({
        .Name = "r.target_fps",
        .Owner = "engine",
        .Type = CVarType::Double,
        .DefaultValue = 0.0,
        .CurrentValue = 0.0,
        .Flags = CVarFlags::Archive,
    }));

    OptionsPage page;
    page.InstallDefaults(registry);
    const std::vector<UiRow> rows = page.Present(registry);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows.front().Value, "Unlimited");
    EXPECT_EQ(rows.front().Control, UiRowControls::Choice);
    EXPECT_EQ(rows.front().Choices.front(), "Unlimited");
    EXPECT_TRUE(rows.front().Editable);
}

TEST(OptionsPageTest, ActivatingARowThatIsNotThereChangesNothing)
{
    // What a click reported against a page that changed under it looks like.
    ConsoleRegistry registry;
    OptionsPage page;
    page.InstallDefaults(registry);
    EXPECT_FALSE(page.Apply(registry, 4, UiValue(0.5)));
}

TEST(OptionsPageTest, AGameCanAddARowOfItsOwn)
{
    ConsoleRegistry registry;
    ASSERT_TRUE(registry.RegisterCVar({
        .Name = "game.difficulty",
        .Owner = "game",
        .Type = CVarType::String,
        .DefaultValue = std::string("normal"),
        .CurrentValue = std::string("normal"),
        .Flags = CVarFlags::Archive,
    }));

    OptionsPage page;
    page.InstallDefaults(registry);
    page.RegisterRows("game", { OptionRow{
        .Label = "Difficulty", .CVar = "game.difficulty", .Control = OptionControl::Choice,
        .Step = 0.0, .Min = 0.0, .Max = 0.0,
        .Choices = { { "Normal", "normal" }, { "Hard", "hard" } } } });

    EXPECT_TRUE(page.Apply(registry, 0, UiValue(std::string("Hard"))));
    EXPECT_EQ(std::get<std::string>(registry.FindCVar("game.difficulty")->CurrentValue), "hard");
    EXPECT_EQ(LabelsOf(page, registry), (std::vector<std::string>{ "Difficulty" }));
}

TEST(OptionsPageTest, RegisteringAnOwnerAgainReplacesItsRowsWhereTheyStood)
{
    const ConsoleRegistry registry;
    OptionsPage page;
    page.RegisterRows("first", { ToggleRow("A"), ToggleRow("B") });
    page.RegisterRows("second", { ToggleRow("C") });
    page.RegisterRows("first", { ToggleRow("D") });
    page.RegisterRows("first", { ToggleRow("D") });
    EXPECT_EQ(LabelsOf(page, registry), (std::vector<std::string>{ "D", "C" }));
}

TEST(OptionsPageTest, UnregisteringAGameLeavesTheDefaultsAndEveryOtherOwner)
{
    ConsoleRegistry registry;
    RegisterReducedFlashing(registry);
    OptionsPage page;
    page.InstallDefaults(registry);
    page.RegisterRows("game.hud", { ToggleRow("Compass") });
    page.RegisterRows("game.audio", { ToggleRow("Music") });

    page.UnregisterOwner("game.hud");
    page.UnregisterOwner("game.hud");
    EXPECT_EQ(LabelsOf(page, registry), (std::vector<std::string>{ "Reduced Flashing", "Music" }));
}

TEST(OptionsPageTest, ReinstallingTheDefaultsKeepsAGamesRowsAfterThem)
{
    ConsoleRegistry registry;
    RegisterReducedFlashing(registry);
    OptionsPage page;
    page.InstallDefaults(registry);
    page.RegisterRows("game", { ToggleRow("Compass") });

    page.InstallDefaults(registry);
    EXPECT_EQ(LabelsOf(page, registry), (std::vector<std::string>{ "Reduced Flashing", "Compass" }));
    page.UnregisterOwner(kEngineOwner);
    EXPECT_EQ(LabelsOf(page, registry), (std::vector<std::string>{ "Compass" }));
}
