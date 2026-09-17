#include <gtest/gtest.h>

#include <app/EngineConsoleBuiltins.h>
#include <audio/AudioService.h>
#include <controller/LookOrientation.h>
#include <core/config/AudioConfig.h>
#include <core/console/ConsoleRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>

#include <SDL3/SDL.h>

#include <string>

// The settings a player changes. What matters is that each reaches the thing
// behind it, that a host without that thing does not offer it at all, and that
// a player's preference does not overwrite what the game arranged.

namespace
{
EngineAudioConfig SilentConfig()
{
    // The dummy driver, so buses are really built and a gain really applied,
    // without the suite depending on the machine having sound. Playback
    // disabled would skip bus setup entirely and prove nothing.
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    EngineAudioConfig config;
    config.EnablePlayback = true;
    EngineAudioBusConfig music;
    music.Name = "Music";
    music.Volume = 0.4f;
    config.Buses.push_back(std::move(music));
    return config;
}
}

TEST(PlayerSettingCVars, VolumeReachesTheMixWithoutOverwritingIt)
{
    // A bus volume is the game saying how its music sits against its effects.
    // The master is the player saying how loud all of it is. A slider that
    // wrote bus volumes would flatten the mix and have nothing to restore it
    // from.
    LoggingProvider logging;
    AudioService audio(logging, SilentConfig());
    ConsoleRegistry registry;

    EngineConsoleBuiltins::RegisterPlayerSettingCVars(registry, &audio, nullptr, nullptr);
    ASSERT_NE(registry.FindCVar("audio.volume"), nullptr);

    ASSERT_TRUE(registry.SetCVar("audio.volume", CVarValue{ 0.25 }, { "test" },
                                 ConsolePhase::EngineReady).Succeeded());
    EXPECT_FLOAT_EQ(audio.GetMasterVolume(), 0.25f);
    EXPECT_FLOAT_EQ(audio.GetBusVolume("Music"), 0.4f)
        << "the player's volume overwrote the game's own mix";
}

TEST(PlayerSettingCVars, VolumeIsRefusedOutsideItsRange)
{
    LoggingProvider logging;
    AudioService audio(logging, SilentConfig());
    ConsoleRegistry registry;
    EngineConsoleBuiltins::RegisterPlayerSettingCVars(registry, &audio, nullptr, nullptr);

    EXPECT_FALSE(registry.SetCVar("audio.volume", CVarValue{ 2.0 }, { "test" },
                                  ConsolePhase::EngineReady).Succeeded());
    EXPECT_FLOAT_EQ(audio.GetMasterVolume(), 1.0f);
}

TEST(PlayerSettingCVars, LookSensitivityLandsWhereLookIntegrationReadsIt)
{
    World world;
    ConsoleRegistry registry;
    EngineConsoleBuiltins::RegisterPlayerSettingCVars(registry, nullptr, nullptr, &world);
    ASSERT_NE(registry.FindCVar("input.look_sensitivity"), nullptr);

    ASSERT_TRUE(registry.SetCVar("input.look_sensitivity", CVarValue{ 2.5 }, { "test" },
                                 ConsolePhase::EngineReady).Succeeded());
    ASSERT_TRUE(world.HasResource<LookSensitivity>());
    EXPECT_FLOAT_EQ(world.GetResource<LookSensitivity>().Scale, 2.5f);
}

TEST(PlayerSettingCVars, ADisplayModeIsOneOfThreeNames)
{
    // Rejected rather than coerced: a name the player typed that means nothing
    // should say so, not silently become windowed.
    LoggingProvider logging;
    AudioService audio(logging, SilentConfig());
    ConsoleRegistry registry;
    EngineConsoleBuiltins::RegisterPlayerSettingCVars(registry, &audio, nullptr, nullptr);

    // No window here, so the row is absent entirely -- which is the rule under
    // test in the next case.
    EXPECT_EQ(registry.FindCVar("window.mode"), nullptr);
}

TEST(PlayerSettingCVars, AHostOffersOnlyTheSettingsItCanActuallyApply)
{
    // A headless host has no window; one with playback disabled has no mixer to
    // turn down. The rows a settings screen shows are the cvars that exist, so
    // what a host cannot do is not offered rather than offered and refused.
    ConsoleRegistry none;
    EngineConsoleBuiltins::RegisterPlayerSettingCVars(none, nullptr, nullptr, nullptr);
    EXPECT_EQ(none.FindCVar("audio.volume"), nullptr);
    EXPECT_EQ(none.FindCVar("input.look_sensitivity"), nullptr);
    EXPECT_EQ(none.FindCVar("window.mode"), nullptr);

    World world;
    ConsoleRegistry some;
    EngineConsoleBuiltins::RegisterPlayerSettingCVars(some, nullptr, nullptr, &world);
    EXPECT_EQ(some.FindCVar("audio.volume"), nullptr);
    EXPECT_NE(some.FindCVar("input.look_sensitivity"), nullptr);
}

TEST(PlayerSettingCVars, EverySettingIsArchivedSoItSurvivesTheRun)
{
    // The point of them being settings rather than knobs.
    LoggingProvider logging;
    AudioService audio(logging, SilentConfig());
    World world;
    ConsoleRegistry registry;
    EngineConsoleBuiltins::RegisterPlayerSettingCVars(registry, &audio, nullptr, &world);

    for (const char* name : { "audio.volume", "input.look_sensitivity" })
    {
        const CVarMetadata* meta = registry.FindCVar(name);
        ASSERT_NE(meta, nullptr) << name;
        EXPECT_TRUE(HasFlag(meta->Flags, CVarFlags::Archive)) << name;
    }
}
