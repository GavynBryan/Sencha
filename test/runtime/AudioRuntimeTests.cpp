#include <gtest/gtest.h>
#include <audio/AudioComponentSchemas.h>

#include <audio/AudioClipCache.h>
#include <audio/AudioService.h>
#include <audio/AudioSourceComponent.h>
#include <audio/AudioSystem.h>
#include <core/assets/AssetRegistry.h>
#include <assets/runtime/AssetSystem.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <world/registry/Registry.h>
#include "HandleFieldIo.h"

#include <world/serialization/SceneAssetFieldIo.h>
#include <world/serialization/SceneFieldCodec.h>
#include <world/serialization/SceneSerializer.h>

#include <SDL3/SDL_hints.h>

#include <cstdint>
#include <string>

namespace
{
AudioClip MakeClip(
    uint32_t sampleRate = 22050,
    uint8_t channels = 1,
    uint32_t frames = 22050)
{
    AudioClip clip;
    clip.SampleRate = sampleRate;
    clip.ChannelCount = channels;
    clip.Samples.assign(
        static_cast<size_t>(frames) * channels,
        1000);
    return clip;
}

AudioClipHandle RegisterResidentClip(
    AssetRegistry& registry,
    AudioClipCache& cache,
    std::string_view path)
{
    registry.Register(AssetRecord{
        .Type = AssetType::Audio,
        .SourceKind = AssetSourceKind::Procedural,
        .Path = std::string(path),
    });
    return cache.Register(path, MakeClip());
}

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

StoragePartitionSet ActivePartitions()
{
    StoragePartitionSet partitions;
    partitions.Add(StoragePartitionId::Default());
    return partitions;
}

void SetupAudioWorld(
    World& world,
    AudioClipCache* cache,
    AudioService* audio)
{
    world.AddResource<AudioSourceRuntime>(cache, audio);
    world.RegisterComponent<AudioSourceComponent>();
}
} // namespace

TEST(AudioClipCodec, SaveWritesPathString)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    AudioClipCache cache(logging);
    AssetSystem assets(
        logging,
        registry,
        nullptr,
        nullptr,
        nullptr,
        &cache);

    const AudioClipHandle handle = RegisterResidentClip(
        registry,
        cache,
        "asset://audio/boop.wav");

    SceneSerializationContext context(logging, &assets);
    JsonWriteArchive archive;
    ASSERT_TRUE(SaveHandleField<AudioClipHandle>(AssetType::Audio,
        archive,
        "clip",
        handle,
        context));

    const JsonValue json = archive.TakeValue();
    ASSERT_TRUE(json.IsString());
    EXPECT_EQ(json.AsString(), "asset://audio/boop.wav");
}

TEST(AudioClipCodec, LoadResolvesPathString)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    AudioClipCache cache(logging);
    AssetSystem assets(
        logging,
        registry,
        nullptr,
        nullptr,
        nullptr,
        &cache);

    const AudioClipHandle registered = RegisterResidentClip(
        registry,
        cache,
        "asset://audio/boop.wav");
    const auto parsed = JsonParse(R"("asset://audio/boop.wav")");
    ASSERT_TRUE(parsed.has_value());

    SceneSerializationContext context(logging, &assets);
    JsonReadArchive archive(*parsed);
    AudioClipHandle loaded;
    ASSERT_TRUE(LoadHandleField<AudioClipHandle>(AssetType::Audio,
        archive,
        "",
        loaded,
        context));
    EXPECT_EQ(loaded, registered);
}

TEST(AudioClipCodec, ComponentRoundTripsThroughSceneJson)
{
    ComponentSerializerRegistry serializers;
    RegisterComponent<AudioSourceComponent>(serializers);

    LoggingProvider logging;
    AssetRegistry registry(logging);
    AudioClipCache cache(logging);
    AssetSystem assets(
        logging,
        registry,
        nullptr,
        nullptr,
        nullptr,
        &cache);
    const AudioClipHandle clip = RegisterResidentClip(
        registry,
        cache,
        "asset://audio/ambient.wav");

    Registry source;
    source.Components.RegisterComponent<AudioSourceComponent>();
    const EntityId entity = source.Components.CreateEntity();
    source.Components.AddComponent(
        entity,
        AudioSourceComponent{
            .Clip = clip,
            .Bus = "Sfx",
            .Gain = 0.5f,
            .Pan = -0.25f,
            .Looping = true,
            .PlayOnActive = false,
        });

    SceneSerializationContext saveContext(logging, &assets);
    const JsonValue json = SaveSceneJson(source, serializers, saveContext);

    Registry destination;
    destination.Components.RegisterComponent<AudioSourceComponent>();
    SceneSerializationContext loadContext(logging, &assets);
    ASSERT_TRUE(LoadSceneJson(json, destination, serializers, loadContext));

    int count = 0;
    destination.Components.ForEachComponent<AudioSourceComponent>(
        [&](EntityId, AudioSourceComponent& component)
    {
        ++count;
        EXPECT_EQ(component.Clip, clip);
        EXPECT_EQ(component.Bus.View(), "Sfx");
        EXPECT_FLOAT_EQ(component.Gain, 0.5f);
        EXPECT_FLOAT_EQ(component.Pan, -0.25f);
        EXPECT_TRUE(component.Looping);
        EXPECT_FALSE(component.PlayOnActive);
        EXPECT_FALSE(component.Voice.IsValid());
        EXPECT_FALSE(component.Started);
    });
    EXPECT_EQ(count, 1);
}

TEST(AudioSourceLifetime, RemoveStopsVoiceBeforeReleasingSoleClipReference)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;
    AudioService audio(logging, SfxConfig());
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    AudioClipCache cache(logging);
    const AudioClipHandle clip = cache.Register(
        "asset://audio/boop.wav",
        MakeClip());

    World world;
    SetupAudioWorld(world, &cache, &audio);
    const EntityId entity = world.CreateEntity();
    world.AddComponent(
        entity,
        AudioSourceComponent{
            .Clip = clip,
            .Bus = "Sfx",
            .Looping = true,
        });
    cache.Release(clip);
    ASSERT_NE(cache.Get(clip), nullptr);

    AudioSystem system;
    const StoragePartitionSet active = ActivePartitions();
    system.Update(&audio, world, active);

    const VoiceId voice =
        world.TryGet<AudioSourceComponent>(entity)->Voice;
    ASSERT_TRUE(voice.IsValid());
    ASSERT_TRUE(audio.IsPlaying(voice));

    world.RemoveComponent<AudioSourceComponent>(entity);
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

    const AudioClip clip = MakeClip();

    PlayParams sfx;
    sfx.Bus = "Sfx";
    const VoiceId first = audio.Play(AudioClipKey{ 1 }, clip, sfx);
    ASSERT_TRUE(first.IsValid());

    const VoiceId second = audio.Play(AudioClipKey{ 2 }, clip, sfx);
    ASSERT_TRUE(second.IsValid());
    EXPECT_FALSE(audio.IsPlaying(first));
    EXPECT_TRUE(audio.IsPlaying(second));

    PlayParams ui;
    ui.Bus = "Ui";
    const VoiceId third = audio.Play(AudioClipKey{ 3 }, clip, ui);
    ASSERT_TRUE(third.IsValid());
    EXPECT_NE(third, second);
    EXPECT_TRUE(audio.IsPlaying(second));
    EXPECT_TRUE(audio.IsPlaying(third));
}

TEST(AudioSystemSweep, LoopRestartsAcrossDormancyAndOneShotDoesNot)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    LoggingProvider logging;
    AudioService audio(logging, SfxConfig());
    if (!audio.IsValid())
        GTEST_SKIP() << "no audio backend";

    AudioClipCache cache(logging);
    const AudioClipHandle clip = cache.Register(
        "asset://audio/ambient.wav",
        MakeClip());

    World world;
    SetupAudioWorld(world, &cache, &audio);

    const EntityId loop = world.CreateEntity();
    world.AddComponent(
        loop,
        AudioSourceComponent{
            .Clip = clip,
            .Bus = "Sfx",
            .Looping = true,
        });
    const EntityId oneShot = world.CreateEntity();
    world.AddComponent(
        oneShot,
        AudioSourceComponent{
            .Clip = clip,
            .Bus = "Sfx",
            .Looping = false,
        });

    AudioSystem system;
    const StoragePartitionSet active = ActivePartitions();
    const StoragePartitionSet dormant;

    system.Update(&audio, world, active);
    const VoiceId firstLoopVoice =
        world.TryGet<AudioSourceComponent>(loop)->Voice;
    const VoiceId firstShotVoice =
        world.TryGet<AudioSourceComponent>(oneShot)->Voice;
    ASSERT_TRUE(firstLoopVoice.IsValid());
    ASSERT_TRUE(firstShotVoice.IsValid());
    EXPECT_TRUE(audio.IsPlaying(firstLoopVoice));
    EXPECT_TRUE(
        world.TryGet<AudioSourceComponent>(oneShot)->Started);

    system.Update(&audio, world, dormant);
    EXPECT_FALSE(audio.IsPlaying(firstLoopVoice));
    EXPECT_FALSE(audio.IsPlaying(firstShotVoice));

    system.Update(&audio, world, active);
    const VoiceId secondLoopVoice =
        world.TryGet<AudioSourceComponent>(loop)->Voice;
    EXPECT_TRUE(secondLoopVoice.IsValid());
    EXPECT_NE(secondLoopVoice, firstLoopVoice);
    EXPECT_TRUE(audio.IsPlaying(secondLoopVoice));

    const AudioSourceComponent* shot =
        world.TryGet<AudioSourceComponent>(oneShot);
    EXPECT_TRUE(shot->Started);
    EXPECT_FALSE(audio.IsPlaying(shot->Voice));
}

TEST(AudioSystemSweep, NullServiceAndPlayOnActiveFalseAreNoOps)
{
    LoggingProvider logging;
    AudioClipCache cache(logging);
    const AudioClipHandle clip = cache.Register(
        "asset://audio/ambient.wav",
        MakeClip());

    World world;
    SetupAudioWorld(world, &cache, nullptr);
    const EntityId entity = world.CreateEntity();
    world.AddComponent(
        entity,
        AudioSourceComponent{
            .Clip = clip,
            .Bus = "Sfx",
            .Looping = true,
            .PlayOnActive = false,
        });

    AudioSystem system;
    const StoragePartitionSet active = ActivePartitions();
    system.Update(nullptr, world, active);
    EXPECT_FALSE(
        world.TryGet<AudioSourceComponent>(entity)->Voice.IsValid());
}

// A process nobody is listening to holds no playback device: a dedicated host
// mixes nothing, and asking for a device it will never use reports a failure
// on a machine with no sound card that was never a problem. The service is
// still constructed, in the same invalid state a failed open leaves, which is
// what every consumer already checks for.
TEST(AudioService, PlaybackDisabledOpensNoDevice)
{
    LoggingProvider logging;
    EngineAudioConfig config;
    config.EnablePlayback = false;
    config.Buses.push_back(EngineAudioBusConfig{ .Name = "Sfx", .MaxVoices = 4 });

    AudioService audio(logging, config);

    EXPECT_FALSE(audio.IsValid())
        << "no device means nothing can play, exactly as a failed open reports";
}
