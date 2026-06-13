#include <gtest/gtest.h>

#include <audio/AudioClipCache.h>
#include <audio/AudioService.h>
#include <audio/AudioSourceComponent.h>
#include <audio/AudioSystem.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneFieldCodec.h>
#include <world/serialization/SceneSerializer.h>

#include <SDL3/SDL_hints.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
    AudioClip MakeClip(uint32_t sampleRate = 22050, uint8_t channels = 1, uint32_t frames = 22050)
    {
        AudioClip clip;
        clip.SampleRate = sampleRate;
        clip.ChannelCount = channels;
        clip.Samples.assign(static_cast<size_t>(frames) * channels, 1000);
        return clip;
    }

    // A clip resident in `cache` plus its Procedural registry record — the
    // smallest setup that lets the codec and the asset front door resolve it.
    AudioClipHandle RegisterResidentClip(AssetRegistry& registry, AudioClipCache& cache,
                                         std::string_view path)
    {
        registry.Register(AssetRecord{
            .Type = AssetType::Audio,
            .SourceKind = AssetSourceKind::Procedural,
            .Path = std::string(path),
        });
        return cache.Register(path, MakeClip());
    }

    // Constructs an AudioService on SDL's dummy driver. Returns nullptr-valid
    // when no audio backend is available (the headless/CI posture) so callers
    // can skip, the Blender-test precedent.
    EngineAudioConfig SfxConfig()
    {
        EngineAudioConfig config;
        EngineAudioBusConfig sfx;
        sfx.Name = "Sfx";
        sfx.MaxVoices = 4;
        sfx.StealPolicy = VoiceStealPolicy::Reject;
        config.Buses.push_back(sfx);
        return config;
    }

    EngineAudioConfig StealConfig()
    {
        EngineAudioConfig config;
        EngineAudioBusConfig sfx;
        sfx.Name = "Sfx";
        sfx.MaxVoices = 1;
        sfx.StealPolicy = VoiceStealPolicy::StealOldest;
        config.Buses.push_back(sfx);

        EngineAudioBusConfig ui;
        ui.Name = "Ui";
        ui.MaxVoices = 1;
        ui.StealPolicy = VoiceStealPolicy::Reject;
        config.Buses.push_back(ui);
        return config;
    }
} // namespace

// -- Codec ----------------------------------------------------------------------

TEST(AudioClipCodec, SaveWritesPathString)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    AudioClipCache cache(logging);
    AssetSystem assets(logging, registry, nullptr, nullptr, nullptr, &cache);

    AudioClipHandle handle = RegisterResidentClip(registry, cache, "asset://audio/boop.wav");

    SceneSerializationContext context(logging, &assets);
    JsonWriteArchive archive;
    ASSERT_TRUE(SceneFieldCodec<AudioClipHandle>::Save(archive, "clip", handle, context));

    JsonValue json = archive.TakeValue();
    ASSERT_TRUE(json.IsString());
    EXPECT_EQ(json.AsString(), "asset://audio/boop.wav");
}

TEST(AudioClipCodec, LoadResolvesPathString)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    AudioClipCache cache(logging);
    AssetSystem assets(logging, registry, nullptr, nullptr, nullptr, &cache);

    AudioClipHandle registered = RegisterResidentClip(registry, cache, "asset://audio/boop.wav");

    auto parsed = JsonParse(R"("asset://audio/boop.wav")");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context(logging, &assets);
    JsonReadArchive archive(*parsed);
    AudioClipHandle loaded;
    ASSERT_TRUE(SceneFieldCodec<AudioClipHandle>::Load(archive, "", loaded, context));
    EXPECT_EQ(loaded, registered);
}

TEST(AudioClipCodec, ComponentRoundTripsThroughSceneJson)
{
    ClearComponentSerializers();
    RegisterComponent<AudioSourceComponent>();

    LoggingProvider logging;
    AssetRegistry registry(logging);
    AudioClipCache cache(logging);
    AssetSystem assets(logging, registry, nullptr, nullptr, nullptr, &cache);
    AudioClipHandle clip = RegisterResidentClip(registry, cache, "asset://audio/ambient.wav");

    Registry src;
    src.Components.RegisterComponent<AudioSourceComponent>();

    EntityId entity = src.Entities.Create();
    src.Components.AddComponent(entity, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Gain = 0.5f, .Pan = -0.25f,
        .Looping = true, .PlayOnActive = false });

    SceneSerializationContext saveCtx(logging, &assets);
    JsonValue json = SaveSceneJson(src, saveCtx);

    Registry dst;
    dst.Components.RegisterComponent<AudioSourceComponent>();

    SceneSerializationContext loadCtx(logging, &assets);
    ASSERT_TRUE(LoadSceneJson(json, dst, loadCtx));

    int count = 0;
    dst.Components.ForEachComponent<AudioSourceComponent>(
        [&](EntityId, AudioSourceComponent& source)
    {
        ++count;
        EXPECT_EQ(source.Clip, clip);
        EXPECT_EQ(source.Bus.View(), "Sfx");
        EXPECT_FLOAT_EQ(source.Gain, 0.5f);
        EXPECT_FLOAT_EQ(source.Pan, -0.25f);
        EXPECT_TRUE(source.Looping);
        EXPECT_FALSE(source.PlayOnActive);
        // Runtime fields are never serialized: they default on load.
        EXPECT_FALSE(source.Voice.IsValid());
        EXPECT_FALSE(source.Started);
    });
    EXPECT_EQ(count, 1);

    ClearComponentSerializers();
}

// -- Lifetime: the slice's one invariant ----------------------------------------

TEST(AudioSourceLifetime, RemoveStopsVoiceBeforeReleasingSoleClipReference)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;
    AudioService audio(logging, SfxConfig());
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    AudioClipCache cache(logging);
    AudioClipHandle clip = cache.Register("asset://audio/boop.wav", MakeClip()); // refcount 1

    Registry registry;
    registry.Components.AddResource<AudioSourceRuntime>(&cache, &audio);
    registry.Components.RegisterComponent<AudioSourceComponent>();

    EntityId entity = registry.Entities.Create();
    registry.Components.AddComponent(entity, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = true });
    // OnAdd retained -> refcount 2. Drop the test's own ref so the component
    // is the sole owner: now releasing first (before stopping) would free the
    // clip out from under a live voice.
    cache.Release(clip);
    ASSERT_NE(cache.Get(clip), nullptr);

    AudioSystem system;
    std::vector<Registry*> active{ &registry };
    system.Update(&audio, active);

    VoiceId voice = registry.Components.TryGet<AudioSourceComponent>(entity)->Voice;
    ASSERT_TRUE(voice.IsValid());
    ASSERT_TRUE(audio.IsPlaying(voice));

    registry.Components.RemoveComponent<AudioSourceComponent>(entity);

    // The voice was stopped, and only then was the last clip reference
    // released — the clip is now gone, and no voice outlived it.
    EXPECT_FALSE(audio.IsPlaying(voice));
    EXPECT_FALSE(cache.Find("asset://audio/boop.wav").IsValid());
}

TEST(AudioServiceVoices, StealOldestSlotIsNotLeftOnFreeList)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;
    AudioService audio(logging, StealConfig());
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    AudioClip clip = MakeClip();

    PlayParams sfx;
    sfx.Bus = "Sfx";

    VoiceId first = audio.Play(AudioClipKey{ 1 }, clip, sfx);
    ASSERT_TRUE(first.IsValid());

    VoiceId second = audio.Play(AudioClipKey{ 2 }, clip, sfx);
    ASSERT_TRUE(second.IsValid());
    EXPECT_FALSE(audio.IsPlaying(first));
    EXPECT_TRUE(audio.IsPlaying(second));

    PlayParams ui;
    ui.Bus = "Ui";

    VoiceId third = audio.Play(AudioClipKey{ 3 }, clip, ui);
    ASSERT_TRUE(third.IsValid());
    EXPECT_NE(third, second);
    EXPECT_TRUE(audio.IsPlaying(second));
    EXPECT_TRUE(audio.IsPlaying(third));
}

// -- AudioSystem: dormancy sweep and start rules --------------------------------

TEST(AudioSystemSweep, LoopRestartsAcrossDormancyAndOneShotDoesNot)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;
    AudioService audio(logging, SfxConfig());
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    AudioClipCache cache(logging);
    AudioClipHandle clip = cache.Register("asset://audio/ambient.wav", MakeClip());

    Registry registry;
    registry.Components.AddResource<AudioSourceRuntime>(&cache, &audio);
    registry.Components.RegisterComponent<AudioSourceComponent>();

    EntityId loop = registry.Entities.Create();
    registry.Components.AddComponent(loop, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = true });
    EntityId oneShot = registry.Entities.Create();
    registry.Components.AddComponent(oneShot, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = false });

    AudioSystem system;
    std::vector<Registry*> active{ &registry };
    std::vector<Registry*> dormant{};

    // Active: both start.
    system.Update(&audio, active);
    VoiceId loopV1 = registry.Components.TryGet<AudioSourceComponent>(loop)->Voice;
    VoiceId shotV1 = registry.Components.TryGet<AudioSourceComponent>(oneShot)->Voice;
    ASSERT_TRUE(loopV1.IsValid());
    ASSERT_TRUE(shotV1.IsValid());
    EXPECT_TRUE(audio.IsPlaying(loopV1));
    EXPECT_TRUE(registry.Components.TryGet<AudioSourceComponent>(oneShot)->Started);

    // Dormant: the sweep stops both voices without touching components.
    system.Update(&audio, dormant);
    EXPECT_FALSE(audio.IsPlaying(loopV1));
    EXPECT_FALSE(audio.IsPlaying(shotV1));

    // Reactivate: the loop starts a fresh voice; the one-shot stays silent
    // (Started latched — re-entry does not replay).
    system.Update(&audio, active);
    VoiceId loopV2 = registry.Components.TryGet<AudioSourceComponent>(loop)->Voice;
    EXPECT_TRUE(loopV2.IsValid());
    EXPECT_NE(loopV2, loopV1);
    EXPECT_TRUE(audio.IsPlaying(loopV2));

    const AudioSourceComponent* shot = registry.Components.TryGet<AudioSourceComponent>(oneShot);
    EXPECT_TRUE(shot->Started);
    EXPECT_FALSE(audio.IsPlaying(shot->Voice));
}

TEST(AudioSystemSweep, NullServiceAndPlayOnActiveFalseAreNoOps)
{
    LoggingProvider logging;
    AudioClipCache cache(logging);
    AudioClipHandle clip = cache.Register("asset://audio/ambient.wav", MakeClip());

    Registry registry;
    registry.Components.AddResource<AudioSourceRuntime>(&cache, nullptr);
    registry.Components.RegisterComponent<AudioSourceComponent>();

    EntityId entity = registry.Entities.Create();
    registry.Components.AddComponent(entity, AudioSourceComponent{
        .Clip = clip, .Bus = "Sfx", .Looping = true, .PlayOnActive = false });

    AudioSystem system;
    std::vector<Registry*> active{ &registry };

    // Null service: the system is a clean no-op (headless).
    system.Update(nullptr, active);
    EXPECT_FALSE(registry.Components.TryGet<AudioSourceComponent>(entity)->Voice.IsValid());
}
