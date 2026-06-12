#include <gtest/gtest.h>

#include <audio/AudioService.h>
#include <audio/Caption.h>
#include <audio/CaptionRuntime.h>
#include <core/config/CaptionConfig.h>
#include <core/logging/LoggingProvider.h>

#include <SDL3/SDL_hints.h>

#include <string_view>

// Stage 1 + 2 of docs/audio/captions-and-dialogue.md: caption state,
// read-time filtering, merge/trim policy, and voice binding. Everything in
// the Filtering/Lifetime suites is fully headless; the VoiceBinding suite
// uses a real AudioService on SDL's dummy driver (skip-if-unavailable, the
// established precedent).

namespace
{
    EngineCaptionConfig TestChannels()
    {
        EngineCaptionConfig config;
        config.Channels.push_back({ .Name = "World", .MaxVisibleLines = 3 });
        config.Channels.push_back({ .Name = "Radio", .MaxVisibleLines = 2 });
        // The dialogue-menu shape: text is ordinary UI, never gated by the
        // accessibility toggles.
        config.Channels.push_back({ .Name = "Menu", .GateOnSettings = false,
                                    .MaxVisibleLines = 0 });
        return config;
    }

    CaptionPayload Subtitle(std::string_view text,
                            std::string_view channel = "World",
                            CaptionPriority priority = CaptionPriority::Gameplay)
    {
        CaptionPayload payload;
        payload.Kind = CaptionKind::Subtitle;
        payload.Channel = channel;
        payload.Priority = priority;
        payload.Text = text;
        return payload;
    }

    CaptionPayload ClosedCaption(std::string_view text,
                                 std::string_view channel = "World")
    {
        CaptionPayload payload;
        payload.Kind = CaptionKind::ClosedCaption;
        payload.Channel = channel;
        payload.Text = text;
        return payload;
    }

    CaptionSettings AllOn()
    {
        CaptionSettings settings;
        settings.SubtitlesEnabled = true;
        settings.ClosedCaptionsEnabled = true;
        return settings;
    }

    CaptionSettings AllOff()
    {
        CaptionSettings settings;
        settings.SubtitlesEnabled = false;
        settings.ClosedCaptionsEnabled = false;
        return settings;
    }

    EngineAudioConfig RejectBusConfig(uint8_t maxVoices = 1)
    {
        EngineAudioConfig config;
        EngineAudioBusConfig sfx;
        sfx.Name = "Sfx";
        sfx.MaxVoices = maxVoices;
        sfx.StealPolicy = VoiceStealPolicy::Reject;
        config.Buses.push_back(sfx);
        return config;
    }

    AudioClip MakeClip(uint32_t frames = 22050)
    {
        AudioClip clip;
        clip.SampleRate = 22050;
        clip.ChannelCount = 1;
        clip.Samples.assign(frames, 1000);
        return clip;
    }
}

// -- Stage 1: read-time filtering -------------------------------------------------

TEST(CaptionFiltering, KindsGateOnSettingsCombinations)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, TestChannels());

    captions.BeginTimedCaption(Subtitle("line.subtitle"), 5.0f);
    captions.BeginTimedCaption(ClosedCaption("cc.door"), 5.0f);

    // Defaults: subtitles on, closed captions off.
    EXPECT_EQ(captions.Visible("World").size(), 1u);
    EXPECT_EQ(captions.Visible("World")[0].Payload.Kind, CaptionKind::Subtitle);

    // Closed captions are the broader mode: they admit speech too.
    CaptionSettings ccOnly = AllOff();
    ccOnly.ClosedCaptionsEnabled = true;
    captions.SetSettings(ccOnly);
    EXPECT_EQ(captions.Visible("World").size(), 2u);

    captions.SetSettings(AllOff());
    EXPECT_EQ(captions.Visible("World").size(), 0u);

    // Read-time filtering: everything stayed stored throughout.
    EXPECT_EQ(captions.ActiveCount(), 2u);
}

TEST(CaptionFiltering, MidLifeSettingsToggleFlipsVisibilityWithoutReEmission)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, TestChannels());
    captions.SetSettings(AllOff());

    CaptionId id = captions.BeginTimedCaption(Subtitle("line.long"), 10.0f);
    ASSERT_TRUE(id.IsValid());
    EXPECT_EQ(captions.Visible("World").size(), 0u);

    // The player turns subtitles on mid-line: the in-flight caption appears
    // immediately — the emission was never dropped.
    CaptionSettings subsOn = AllOff();
    subsOn.SubtitlesEnabled = true;
    captions.SetSettings(subsOn);
    ASSERT_EQ(captions.Visible("World").size(), 1u);
    EXPECT_EQ(captions.Visible("World")[0].Id, id);

    // And off again: gone from the visible list, still active.
    captions.SetSettings(AllOff());
    EXPECT_EQ(captions.Visible("World").size(), 0u);
    EXPECT_TRUE(captions.IsActive(id));
}

TEST(CaptionFiltering, UngatedChannelIgnoresAccessibilityToggles)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, TestChannels());
    captions.SetSettings(AllOff());

    captions.BeginTimedCaption(Subtitle("menu.line", "Menu"), 5.0f);
    EXPECT_EQ(captions.Visible("Menu").size(), 1u);
    // ...and it never leaks into a gated channel.
    EXPECT_EQ(captions.Visible("World").size(), 0u);
}

TEST(CaptionFiltering, UnknownChannelSuppressesAndKindNoneNeverStored)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, TestChannels());

    EXPECT_FALSE(captions.BeginTimedCaption(Subtitle("x", "Typo"), 5.0f).IsValid());

    CaptionPayload none;
    none.Kind = CaptionKind::None;
    none.Text = "authored.no.caption";
    EXPECT_FALSE(captions.BeginTimedCaption(none, 5.0f).IsValid());

    CaptionPayload empty = Subtitle("");
    EXPECT_FALSE(captions.BeginTimedCaption(empty, 5.0f).IsValid());

    EXPECT_EQ(captions.ActiveCount(), 0u);
}

TEST(CaptionFiltering, DefaultChannelSetInstalledWhenConfigEmpty)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, EngineCaptionConfig{});

    captions.BeginTimedCaption(Subtitle("a", "World"), 5.0f);
    captions.BeginTimedCaption(Subtitle("b", "Cutscene"), 5.0f);
    captions.BeginTimedCaption(Subtitle("c", "UI"), 5.0f);
    EXPECT_EQ(captions.Visible("World").size(), 1u);
    EXPECT_EQ(captions.Visible("Cutscene").size(), 1u);
    EXPECT_EQ(captions.Visible("UI").size(), 1u);
}

// -- Stage 1: priority, trimming, merging ----------------------------------------

TEST(CaptionPolicy, PriorityThenRecencyTrimIsDeterministic)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, TestChannels());

    // World clamps at 3 lines. Five captions, mixed priorities; duplicates
    // disabled so each is its own line.
    auto begin = [&](std::string_view text, CaptionPriority priority)
    {
        CaptionPayload payload = Subtitle(text, "World", priority);
        payload.MergeDuplicates = false;
        return captions.BeginTimedCaption(payload, 30.0f);
    };

    begin("ambient.1", CaptionPriority::Ambient);
    begin("gameplay.1", CaptionPriority::Gameplay);
    CaptionId critical = begin("critical.1", CaptionPriority::Critical);
    begin("gameplay.2", CaptionPriority::Gameplay);
    CaptionId narrative = begin("narrative.1", CaptionPriority::Narrative);

    auto visible = captions.Visible("World");
    ASSERT_EQ(visible.size(), 3u);
    // Priority outranks: Critical, then Narrative, then the newest Gameplay.
    EXPECT_EQ(visible[0].Id, critical);
    EXPECT_EQ(visible[1].Id, narrative);
    EXPECT_EQ(visible[2].Payload.Text.View(), "gameplay.2");

    // The trim is presentation only — nothing was retired.
    EXPECT_EQ(captions.ActiveCount(), 5u);
}

TEST(CaptionPolicy, DuplicateMergeRefreshesInsteadOfStacking)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, TestChannels());
    captions.SetSettings(AllOn());

    CaptionId first = captions.BeginTimedCaption(ClosedCaption("cc.gunfire"), 2.0f);
    ASSERT_TRUE(first.IsValid());
    captions.Tick(nullptr, 1.5f);

    // Same text/speaker/channel: refreshed, not stacked — [gunfire] must
    // not add thirty lines.
    CaptionId second = captions.BeginTimedCaption(ClosedCaption("cc.gunfire"), 2.0f);
    EXPECT_EQ(second, first);
    EXPECT_EQ(captions.ActiveCount(), 1u);

    // The refresh reset the age: 1.5s later it is still alive (a non-merged
    // caption would have expired at 2.0s total).
    captions.Tick(nullptr, 1.5f);
    EXPECT_TRUE(captions.IsActive(first));

    // Opting out per payload stacks normally.
    CaptionPayload noMerge = ClosedCaption("cc.gunfire");
    noMerge.MergeDuplicates = false;
    CaptionId third = captions.BeginTimedCaption(noMerge, 2.0f);
    EXPECT_NE(third, first);
    EXPECT_EQ(captions.ActiveCount(), 2u);
}

TEST(CaptionPolicy, DifferentSpeakersDoNotMerge)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, TestChannels());

    CaptionPayload a = Subtitle("line.hello");
    a.Speaker = "alice";
    CaptionPayload b = Subtitle("line.hello");
    b.Speaker = "bob";

    CaptionId first = captions.BeginTimedCaption(a, 5.0f);
    CaptionId second = captions.BeginTimedCaption(b, 5.0f);
    EXPECT_NE(first, second);
    EXPECT_EQ(captions.ActiveCount(), 2u);
}

// -- Stage 1: lifetime ----------------------------------------------------------

TEST(CaptionLifetime, TimedCaptionExpiresAndIdsAreStaleSafe)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, TestChannels());

    CaptionId id = captions.BeginTimedCaption(Subtitle("line.brief"), 1.0f);
    ASSERT_TRUE(captions.IsActive(id));

    captions.Tick(nullptr, 0.5f);
    EXPECT_TRUE(captions.IsActive(id));

    captions.Tick(nullptr, 0.6f);
    EXPECT_FALSE(captions.IsActive(id));

    // Double-end is a no-op (zone churn safety), and a stale id never
    // touches a slot reused by a newer caption.
    captions.EndCaption(id);
    CaptionId reused = captions.BeginTimedCaption(Subtitle("line.next"), 5.0f);
    EXPECT_NE(reused, id);
    captions.EndCaption(id);
    EXPECT_TRUE(captions.IsActive(reused));
}

TEST(CaptionLifetime, TimedDurationDerivesFromClampWhenUnspecified)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, TestChannels());

    // No duration anywhere: the reading-speed estimate clamps to the
    // settings minimum for a short key.
    CaptionId id = captions.BeginTimedCaption(Subtitle("hi"), 0.0f);
    auto active = captions.Active();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_FLOAT_EQ(active[0].DurationSeconds,
                    captions.Settings().DerivedDurationMinSeconds);
    EXPECT_TRUE(id.IsValid());
}

// -- Stage 2: voice binding -------------------------------------------------------

TEST(CaptionVoiceBinding, VoiceStopRetiresCaption)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;
    AudioService audio(logging, RejectBusConfig(4));
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    CaptionRuntime captions(logging, TestChannels());
    AudioClip clip = MakeClip();

    PlayParams params;
    params.Bus = "Sfx";
    VoiceId voice = audio.Play(AssetId{ 1 }, clip, params);
    ASSERT_TRUE(voice.IsValid());

    CaptionId id = captions.BeginVoiceCaption(voice, Subtitle("line.radio"));
    ASSERT_TRUE(id.IsValid());

    captions.Tick(&audio, 0.016f);
    EXPECT_TRUE(captions.IsActive(id));

    audio.Stop(voice);
    captions.Tick(&audio, 0.016f);
    EXPECT_FALSE(captions.IsActive(id));
}

TEST(CaptionVoiceBinding, VoiceStealRetiresCaption)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;

    EngineAudioConfig config;
    EngineAudioBusConfig sfx;
    sfx.Name = "Sfx";
    sfx.MaxVoices = 1;
    sfx.StealPolicy = VoiceStealPolicy::StealOldest;
    config.Buses.push_back(sfx);

    AudioService audio(logging, config);
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    CaptionRuntime captions(logging, TestChannels());
    AudioClip clip = MakeClip();

    PlayParams params;
    params.Bus = "Sfx";
    VoiceId first = audio.Play(AssetId{ 1 }, clip, params);
    ASSERT_TRUE(first.IsValid());
    CaptionId id = captions.BeginVoiceCaption(first, Subtitle("line.stolen"));
    ASSERT_TRUE(id.IsValid());

    // The steal invalidates the first voice; its caption falls out on Tick
    // with no caption-aware code anywhere near the steal.
    VoiceId second = audio.Play(AssetId{ 2 }, clip, params);
    ASSERT_TRUE(second.IsValid());
    captions.Tick(&audio, 0.016f);
    EXPECT_FALSE(captions.IsActive(id));
}

TEST(CaptionVoiceBinding, RejectedPlayStillSubtitlesButDropsClosedCaption)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;
    AudioService audio(logging, RejectBusConfig(1));
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    CaptionRuntime captions(logging, TestChannels());
    captions.SetSettings(AllOn());
    AudioClip clip = MakeClip();

    PlayParams params;
    params.Bus = "Sfx";
    VoiceId occupant = audio.Play(AssetId{ 1 }, clip, params);
    ASSERT_TRUE(occupant.IsValid());

    // The bus is full on a Reject policy — the common failure, not the
    // exotic one. Language content survives as a timed caption; a closed
    // caption for a sound that never happened would lie, so it is dropped.
    VoiceId rejected = audio.Play(AssetId{ 2 }, clip, params);
    ASSERT_FALSE(rejected.IsValid());

    CaptionId subtitle = captions.BeginVoiceCaption(
        rejected, Subtitle("line.must.survive"), {}, /*clipDurationHint*/ 3.0f);
    ASSERT_TRUE(subtitle.IsValid());

    auto active = captions.Active();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_FALSE(active[0].Voice.IsValid());          // degraded to timed
    EXPECT_FLOAT_EQ(active[0].DurationSeconds, 3.0f); // clip duration hint

    CaptionId cc = captions.BeginVoiceCaption(rejected, ClosedCaption("cc.dropped"));
    EXPECT_FALSE(cc.IsValid());
    EXPECT_EQ(captions.ActiveCount(), 1u);
}

TEST(CaptionVoiceBinding, AuthoredDurationCapsLiveVoiceCaption)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;
    AudioService audio(logging, RejectBusConfig(4));
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    CaptionRuntime captions(logging, TestChannels());
    AudioClip clip = MakeClip();

    PlayParams params;
    params.Bus = "Sfx";
    params.Looping = true;
    VoiceId voice = audio.Play(AssetId{ 1 }, clip, params);
    ASSERT_TRUE(voice.IsValid());

    // A looping generator with an authored finite caption: the voice plays
    // on, the caption expires at its cap.
    CaptionPayload payload = ClosedCaption("cc.generator");
    payload.DurationSeconds = 2.0f;
    CaptionSettings ccOn = AllOn();
    captions.SetSettings(ccOn);

    CaptionId id = captions.BeginVoiceCaption(voice, payload);
    ASSERT_TRUE(id.IsValid());

    captions.Tick(&audio, 2.1f);
    EXPECT_FALSE(captions.IsActive(id));
    EXPECT_TRUE(audio.IsPlaying(voice));
}

TEST(CaptionVoiceBinding, NullAudioServiceDegradesIdentically)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, TestChannels());
    captions.SetSettings(AllOn());

    // No device at all (the headless/broken-audio posture): subtitles still
    // arrive — exactly when a player needs text most — and CC stays silent.
    CaptionId subtitle = captions.BeginVoiceCaption({}, Subtitle("line.no.device"));
    EXPECT_TRUE(subtitle.IsValid());
    CaptionId cc = captions.BeginVoiceCaption({}, ClosedCaption("cc.no.device"));
    EXPECT_FALSE(cc.IsValid());

    // A voice-bound caption with a null service in Tick retires by duration
    // or explicit end only — here the derived duration.
    captions.Tick(nullptr, captions.Settings().DerivedDurationMaxSeconds + 0.1f);
    EXPECT_FALSE(captions.IsActive(subtitle));
}
