#include <gtest/gtest.h>

#include <audio/AudioCaptionComponent.h>
#include <audio/AudioClipCache.h>
#include <audio/AudioService.h>
#include <audio/AudioSourceComponent.h>
#include <audio/AudioSystem.h>
#include <audio/CaptionRuntime.h>
#include <audio/CaptionSystem.h>
#include <core/config/CaptionConfig.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneFieldCodec.h>
#include <world/serialization/SceneSerializer.h>

#include <SDL3/SDL_hints.h>

#include <string_view>
#include <vector>

// Stage 3 + 4 of docs/audio/captions-and-dialogue.md: scene composition
// (AudioCaptionComponent beside AudioSourceComponent, driven by
// CaptionSystem after AudioSystem) and the presenter-proof gate.

namespace
{
    AudioClip MakeClip(uint32_t frames = 22050)
    {
        AudioClip clip;
        clip.SampleRate = 22050;
        clip.ChannelCount = 1;
        clip.Samples.assign(frames, 1000);
        return clip;
    }

    EngineAudioConfig SfxConfig(uint8_t maxVoices = 4)
    {
        EngineAudioConfig config;
        EngineAudioBusConfig sfx;
        sfx.Name = "Sfx";
        sfx.MaxVoices = maxVoices;
        sfx.StealPolicy = VoiceStealPolicy::Reject;
        config.Buses.push_back(sfx);
        return config;
    }

    EngineCaptionConfig GateChannels()
    {
        EngineCaptionConfig config;
        config.Channels.push_back({ .Name = "World", .MaxVisibleLines = 3 });
        config.Channels.push_back({ .Name = "Radio", .MaxVisibleLines = 2 });
        config.Channels.push_back({ .Name = "DialogueMenu", .GateOnSettings = false,
                                    .MaxVisibleLines = 0 });
        return config;
    }

    CaptionSettings Everything()
    {
        CaptionSettings settings;
        settings.SubtitlesEnabled = true;
        settings.ClosedCaptionsEnabled = true;
        return settings;
    }

    void SetupRegistry(Registry& registry, AudioClipCache* cache,
                       AudioService* audio, CaptionRuntime* captions)
    {
        registry.Components.AddResource<AudioSourceRuntime>(cache, audio, captions);
        registry.Components.RegisterComponent<AudioSourceComponent>();
        registry.Components.RegisterComponent<AudioCaptionComponent>();
    }

    AudioCaptionComponent WorldCC(std::string_view text)
    {
        return AudioCaptionComponent{
            .Kind = CaptionKind::ClosedCaption,
            .Channel = "World",
            .Text = CaptionTextKey(text),
        };
    }
}

// -- Composition: source + caption together --------------------------------------

TEST(CaptionSystemScene, ActiveZoneStartsSourceAndCaptionTogether)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;
    AudioService audio(logging, SfxConfig());
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    CaptionRuntime captions(logging, GateChannels());
    captions.SetSettings(Everything());
    AudioClipCache cache(logging);
    AudioClipHandle clip = cache.Register("asset://audio/hum.wav", MakeClip());

    Registry registry;
    SetupRegistry(registry, &cache, &audio, &captions);

    EntityId entity = registry.Entities.Create();
    registry.Components.AddComponent(entity, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = true });
    registry.Components.AddComponent(entity, WorldCC("cc.hum"));

    AudioSystem audioSystem;
    CaptionSystem captionSystem;
    std::vector<Registry*> active{ &registry };

    audioSystem.Update(&audio, active);
    captionSystem.Update(&captions, &audio, active, 0.016f);

    const auto* source = registry.Components.TryGet<AudioSourceComponent>(entity);
    const auto* caption = registry.Components.TryGet<AudioCaptionComponent>(entity);
    ASSERT_TRUE(source->Voice.IsValid());
    ASSERT_TRUE(caption->Caption.IsValid());
    EXPECT_EQ(caption->CaptionedVoice, source->Voice);

    auto visible = captions.Visible("World");
    ASSERT_EQ(visible.size(), 1u);
    EXPECT_EQ(visible[0].Payload.Text.View(), "cc.hum");
    EXPECT_EQ(visible[0].Voice, source->Voice);
    EXPECT_EQ(visible[0].Source.Entity, entity);

    // A raw source without a caption component never captions — the second
    // invariant, by construction.
    EXPECT_EQ(captions.ActiveCount(), 1u);
}

TEST(CaptionSystemScene, DormancyRetiresCaptionAndLoopReentryRecaptions)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;
    AudioService audio(logging, SfxConfig());
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    CaptionRuntime captions(logging, GateChannels());
    captions.SetSettings(Everything());
    AudioClipCache cache(logging);
    AudioClipHandle clip = cache.Register("asset://audio/amb.wav", MakeClip());

    Registry registry;
    SetupRegistry(registry, &cache, &audio, &captions);

    EntityId loop = registry.Entities.Create();
    registry.Components.AddComponent(loop, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = true });
    registry.Components.AddComponent(loop, WorldCC("cc.loop"));

    EntityId oneShot = registry.Entities.Create();
    registry.Components.AddComponent(oneShot, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = false });
    registry.Components.AddComponent(oneShot, WorldCC("cc.oneshot"));

    AudioSystem audioSystem;
    CaptionSystem captionSystem;
    std::vector<Registry*> active{ &registry };
    std::vector<Registry*> dormant{};

    // Active: both caption.
    audioSystem.Update(&audio, active);
    captionSystem.Update(&captions, &audio, active, 0.016f);
    EXPECT_EQ(captions.Visible("World").size(), 2u);

    // Dormant: the audio sweep stops the voices; the captions fall out of
    // CaptionRuntime::Tick via voice state — no caption-aware sweep exists.
    audioSystem.Update(&audio, dormant);
    captionSystem.Update(&captions, &audio, dormant, 0.016f);
    EXPECT_EQ(captions.ActiveCount(), 0u);

    // Reactivate: the loop restarts with a fresh voice and re-captions; the
    // one-shot's Started latch holds and neither sound nor caption replays.
    audioSystem.Update(&audio, active);
    captionSystem.Update(&captions, &audio, active, 0.016f);

    auto visible = captions.Visible("World");
    ASSERT_EQ(visible.size(), 1u);
    EXPECT_EQ(visible[0].Payload.Text.View(), "cc.loop");
    EXPECT_EQ(visible[0].Voice,
              registry.Components.TryGet<AudioSourceComponent>(loop)->Voice);
}

TEST(CaptionSystemScene, ComponentRemovalEndsCaption)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;
    AudioService audio(logging, SfxConfig());
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    CaptionRuntime captions(logging, GateChannels());
    captions.SetSettings(Everything());
    AudioClipCache cache(logging);
    AudioClipHandle clip = cache.Register("asset://audio/door.wav", MakeClip());

    Registry registry;
    SetupRegistry(registry, &cache, &audio, &captions);

    EntityId entity = registry.Entities.Create();
    registry.Components.AddComponent(entity, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = true });
    registry.Components.AddComponent(entity, WorldCC("cc.door"));

    AudioSystem audioSystem;
    CaptionSystem captionSystem;
    std::vector<Registry*> active{ &registry };
    audioSystem.Update(&audio, active);
    captionSystem.Update(&captions, &audio, active, 0.016f);

    CaptionId id = registry.Components.TryGet<AudioCaptionComponent>(entity)->Caption;
    ASSERT_TRUE(captions.IsActive(id));

    // OnRemove ends the caption directly — entity destruction and zone
    // detach both fire it, and it does not wait for the next Tick.
    registry.Components.RemoveComponent<AudioCaptionComponent>(entity);
    EXPECT_FALSE(captions.IsActive(id));
}

TEST(CaptionSystemScene, OrphanCaptionComponentIsInert)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, GateChannels());

    Registry registry;
    SetupRegistry(registry, nullptr, nullptr, &captions);

    EntityId entity = registry.Entities.Create();
    registry.Components.AddComponent(entity, WorldCC("cc.orphan"));

    CaptionSystem captionSystem;
    std::vector<Registry*> active{ &registry };
    captionSystem.Update(&captions, nullptr, active, 0.016f);
    captionSystem.Update(&captions, nullptr, active, 0.016f);

    EXPECT_EQ(captions.ActiveCount(), 0u);
    EXPECT_TRUE(registry.Components.TryGet<AudioCaptionComponent>(entity)->WarnedOrphan);
}

// -- Decision C on the scene path -------------------------------------------------

TEST(CaptionSystemDegrade, RejectedSceneSubtitleDegradesToTimedAndCCDrops)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;
    AudioService audio(logging, SfxConfig(1));
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    CaptionRuntime captions(logging, GateChannels());
    captions.SetSettings(Everything());
    AudioClipCache cache(logging);
    AudioClipHandle clip = cache.Register("asset://audio/line.wav", MakeClip());

    // Saturate the single Reject voice before the scene sources try.
    AudioClip occupantClip = MakeClip();
    PlayParams params;
    params.Bus = "Sfx";
    params.Looping = true;
    VoiceId occupant = audio.Play(AssetId{ 99 }, occupantClip, params);
    ASSERT_TRUE(occupant.IsValid());

    Registry registry;
    SetupRegistry(registry, &cache, &audio, &captions);

    EntityId speech = registry.Entities.Create();
    registry.Components.AddComponent(speech, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = false });
    registry.Components.AddComponent(speech, AudioCaptionComponent{
        .Kind = CaptionKind::Subtitle,
        .Channel = "World",
        .Priority = CaptionPriority::Narrative,
        .Text = "line.rejected",
    });

    EntityId slam = registry.Entities.Create();
    registry.Components.AddComponent(slam, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = false });
    registry.Components.AddComponent(slam, WorldCC("cc.rejected"));

    AudioSystem audioSystem;
    CaptionSystem captionSystem;
    std::vector<Registry*> active{ &registry };
    audioSystem.Update(&audio, active);
    captionSystem.Update(&captions, &audio, active, 0.016f);

    // Both sources attempted and were rejected. The subtitle survived as a
    // timed caption; the closed caption stayed silent (no sound happened).
    auto visible = captions.Visible("World");
    ASSERT_EQ(visible.size(), 1u);
    EXPECT_EQ(visible[0].Payload.Text.View(), "line.rejected");
    EXPECT_FALSE(visible[0].Voice.IsValid());
    EXPECT_GT(visible[0].DurationSeconds, 0.0f);

    // The degrade fires once per component lifetime, not per frame.
    captionSystem.Update(&captions, &audio, active, 0.016f);
    EXPECT_EQ(captions.ActiveCount(), 1u);
}

TEST(CaptionSystemDegrade, NoAudioServiceStillSubtitlesActiveSources)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, GateChannels());
    AudioClipCache cache(logging);
    AudioClipHandle clip = cache.Register("asset://audio/voice.wav", MakeClip());

    Registry registry;
    SetupRegistry(registry, &cache, nullptr, &captions);

    EntityId entity = registry.Entities.Create();
    registry.Components.AddComponent(entity, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = false });
    registry.Components.AddComponent(entity, AudioCaptionComponent{
        .Kind = CaptionKind::Subtitle,
        .Channel = "World",
        .Text = "line.no.device",
    });

    // No audio service at all: AudioSystem never runs, but the player still
    // gets the language content — the third invariant.
    AudioSystem audioSystem;
    CaptionSystem captionSystem;
    std::vector<Registry*> active{ &registry };
    audioSystem.Update(nullptr, active);
    captionSystem.Update(&captions, nullptr, active, 0.016f);

    auto visible = captions.Visible("World");
    ASSERT_EQ(visible.size(), 1u);
    EXPECT_EQ(visible[0].Payload.Text.View(), "line.no.device");
}

// -- Scene serialization -----------------------------------------------------------

TEST(CaptionSceneCodec, ComponentRoundTripsWithReadableEnumStrings)
{
    ClearComponentSerializers();
    RegisterComponent<AudioCaptionComponent>();

    LoggingProvider logging;

    Registry src;
    src.Components.RegisterComponent<AudioCaptionComponent>();
    EntityId entity = src.Entities.Create();
    src.Components.AddComponent(entity, AudioCaptionComponent{
        .Kind = CaptionKind::Subtitle,
        .Channel = "Radio",
        .Priority = CaptionPriority::Narrative,
        .Text = "radio.bridge.open",
        .Speaker = "operator",
        .DurationSeconds = 4.5f,
        .MergeDuplicates = false,
    });

    SceneSerializationContext saveCtx(logging);
    JsonValue json = SaveSceneJson(src, saveCtx);

    // Author-readable strings in the scene file, never raw integers.
    const JsonValue* components =
        json.Find("entities")->AsArray()[0].Find("components");
    const JsonValue* chunk = components->Find("AudioCaption");
    ASSERT_NE(chunk, nullptr);
    EXPECT_EQ(chunk->Find("kind")->AsString(), "Subtitle");
    EXPECT_EQ(chunk->Find("priority")->AsString(), "Narrative");

    Registry dst;
    dst.Components.RegisterComponent<AudioCaptionComponent>();
    SceneSerializationContext loadCtx(logging);
    ASSERT_TRUE(LoadSceneJson(json, dst, loadCtx));

    int count = 0;
    dst.Components.ForEachComponent<AudioCaptionComponent>(
        [&](EntityId, AudioCaptionComponent& caption)
    {
        ++count;
        EXPECT_EQ(caption.Kind, CaptionKind::Subtitle);
        EXPECT_EQ(caption.Channel.View(), "Radio");
        EXPECT_EQ(caption.Priority, CaptionPriority::Narrative);
        EXPECT_EQ(caption.Text.View(), "radio.bridge.open");
        EXPECT_EQ(caption.Speaker.View(), "operator");
        EXPECT_FLOAT_EQ(caption.DurationSeconds, 4.5f);
        EXPECT_FALSE(caption.MergeDuplicates);
        // Runtime fields are never serialized: they default on load.
        EXPECT_FALSE(caption.Caption.IsValid());
        EXPECT_FALSE(caption.CaptionedVoice.IsValid());
        EXPECT_FALSE(caption.CaptionAttempted);
    });
    EXPECT_EQ(count, 1);

    ClearComponentSerializers();
}

TEST(CaptionSceneCodec, UnknownEnumStringFailsLoad)
{
    LoggingProvider logging;
    SceneSerializationContext context(logging);

    auto parsed = JsonParse(R"("Banana")");
    ASSERT_TRUE(parsed.has_value());

    JsonReadArchive archive(*parsed);
    CaptionKind kind = CaptionKind::None;
    EXPECT_FALSE(SceneFieldCodec<CaptionKind>::Load(archive, "", kind, context));
}

// -- Stage 4 gate -------------------------------------------------------------------

// The plan's gate: one captioned world SFX, one radio subtitle on a
// game-defined channel, and one ungated dialogue-menu line, each landing
// only in its own channel; the accessibility toggles flip exactly the gated
// ones; visible ordering is deterministic.
TEST(CaptionGate, ThreeContextsRouteFilterAndOrderDeterministically)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;
    AudioService audio(logging, SfxConfig());
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    CaptionRuntime captions(logging, GateChannels());
    captions.SetSettings(Everything());
    AudioClipCache cache(logging);
    AudioClipHandle clip = cache.Register("asset://audio/world.wav", MakeClip());

    // World SFX: scene-authored closed caption.
    Registry registry;
    SetupRegistry(registry, &cache, &audio, &captions);
    EntityId door = registry.Entities.Create();
    registry.Components.AddComponent(door, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = true });
    registry.Components.AddComponent(door, WorldCC("cc.door.slam"));

    AudioSystem audioSystem;
    CaptionSystem captionSystem;
    std::vector<Registry*> active{ &registry };
    audioSystem.Update(&audio, active);
    captionSystem.Update(&captions, &audio, active, 0.016f);

    // Radio subtitle: imperative voice-bound caption on a game-defined
    // channel — channels-as-config end to end.
    AudioClip radioClip = MakeClip();
    PlayParams params;
    params.Bus = "Sfx";
    params.Looping = true;
    VoiceId radioVoice = audio.Play(AssetId{ 7 }, radioClip, params);
    ASSERT_TRUE(radioVoice.IsValid());

    CaptionPayload radioLine;
    radioLine.Kind = CaptionKind::Subtitle;
    radioLine.Channel = "Radio";
    radioLine.Priority = CaptionPriority::Narrative;
    radioLine.Text = "radio.bridge.open";
    radioLine.Speaker = "operator";
    ASSERT_TRUE(captions.BeginVoiceCaption(radioVoice, radioLine).IsValid());

    // Dialogue-menu line: timed, on the ungated channel.
    CaptionPayload menuLine;
    menuLine.Kind = CaptionKind::Subtitle;
    menuLine.Channel = "DialogueMenu";
    menuLine.Text = "menu.choice.leave";
    ASSERT_TRUE(captions.BeginTimedCaption(menuLine, 30.0f).IsValid());

    // Each caption lands only in its own channel.
    ASSERT_EQ(captions.Visible("World").size(), 1u);
    EXPECT_EQ(captions.Visible("World")[0].Payload.Text.View(), "cc.door.slam");
    ASSERT_EQ(captions.Visible("Radio").size(), 1u);
    EXPECT_EQ(captions.Visible("Radio")[0].Payload.Text.View(), "radio.bridge.open");
    ASSERT_EQ(captions.Visible("DialogueMenu").size(), 1u);
    EXPECT_EQ(captions.Visible("DialogueMenu")[0].Payload.Text.View(), "menu.choice.leave");

    // All settings off: exactly the gated channels empty; the menu keeps its
    // text (it is ordinary UI, not an accessibility overlay).
    CaptionSettings off;
    off.SubtitlesEnabled = false;
    off.ClosedCaptionsEnabled = false;
    captions.SetSettings(off);
    EXPECT_EQ(captions.Visible("World").size(), 0u);
    EXPECT_EQ(captions.Visible("Radio").size(), 0u);
    EXPECT_EQ(captions.Visible("DialogueMenu").size(), 1u);

    // Subtitles only: radio speech returns, the world CC stays hidden.
    CaptionSettings subsOnly;
    subsOnly.SubtitlesEnabled = true;
    subsOnly.ClosedCaptionsEnabled = false;
    captions.SetSettings(subsOnly);
    EXPECT_EQ(captions.Visible("World").size(), 0u);
    EXPECT_EQ(captions.Visible("Radio").size(), 1u);

    // Determinism: the same state reads back in the same order after a
    // forced snapshot rebuild.
    captions.SetSettings(Everything());
    std::vector<uint32_t> firstOrder;
    for (const ActiveCaption& caption : captions.Active())
        firstOrder.push_back(caption.Id.Id);
    captions.Tick(&audio, 0.0f);
    std::vector<uint32_t> secondOrder;
    for (const ActiveCaption& caption : captions.Active())
        secondOrder.push_back(caption.Id.Id);
    EXPECT_EQ(firstOrder, secondOrder);
    EXPECT_EQ(captions.ActiveCount(), 3u);
}
