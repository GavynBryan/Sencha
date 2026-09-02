// An asset a component names is held for exactly as long as the component
// holds it: one reference, taken when the component arrives and dropped when
// it leaves, no matter which path put it there.
//
// The paths differ in how the handle is produced -- a scene load resolves a
// path through the asset system, code assigns a handle it already holds -- and
// they have to agree, because the same component is destroyed the same way in
// both cases.

#include <gtest/gtest.h>

#include <assets/audio_clip/AudioClipAssetLoader.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/runtime/RegisterAssetKind.h>
#include <audio/AudioClipCache.h>
#include <core/assets/AssetLease.h>
#include <audio/AudioSourceComponent.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <world/registry/Registry.h>
#include <world/registry/SceneRegistryInitialization.h>
#include <world/serialization/SceneSerializer.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view kClipPath = "asset://audio/test/tone.wav";

    // A four-sample mono PCM wave. Written rather than fixtured because the
    // asset under test only has to be loadable and releasable; nothing here
    // listens to it.
    void WriteTinyWav(const std::filesystem::path& path)
    {
        const std::int16_t samples[4] = { 0, 8000, 0, -8000 };
        const std::uint32_t dataBytes = sizeof(samples);
        const std::uint32_t sampleRate = 8000;
        const std::uint16_t channels = 1;
        const std::uint16_t bitsPerSample = 16;
        const std::uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
        const std::uint16_t blockAlign = channels * bitsPerSample / 8;

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const auto u32 = [&out](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
        const auto u16 = [&out](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };

        out.write("RIFF", 4);
        u32(36 + dataBytes);
        out.write("WAVE", 4);
        out.write("fmt ", 4);
        u32(16);
        u16(1); // PCM
        u16(channels);
        u32(sampleRate);
        u32(byteRate);
        u16(blockAlign);
        u16(bitsPerSample);
        out.write("data", 4);
        u32(dataBytes);
        out.write(reinterpret_cast<const char*>(samples), dataBytes);
    }

    class ComponentAssetLifetimeTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            File = std::filesystem::temp_directory_path()
                / ("sencha_component_asset_lifetime_"
                   + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".wav");
            WriteTinyWav(File);

            AssetRecord record;
            record.Type = AssetType::Audio;
            record.SourceKind = AssetSourceKind::File;
            record.Path = std::string(kClipPath);
            record.FilePath = File.generic_string();
            ASSERT_TRUE(Registry_.Register(record));
            ASSERT_FALSE(Clips.IsResident(kClipPath));

            RegisterAssetKind(Assets, AssetType::Audio, ClipLoader, &Clips);
            InitializeSceneRegistry(Scene, Assets.Stores());
            RegisterEngineSceneSerializers(Serializers);
        }

        void TearDown() override
        {
            std::error_code ec;
            std::filesystem::remove(File, ec);
        }

        std::filesystem::path File;
        LoggingProvider Logging;
        AssetRegistry Registry_{ Logging };
        AudioClipCache Clips{ Logging };
        AudioClipAssetLoader ClipLoader{ Logging, &Clips };
        AssetSystem Assets{ Logging, Registry_ };
        Registry Scene;
        ComponentSerializerRegistry Serializers;
    };
}

// The reference a component owns is the one its lifecycle hooks take, so an
// entity built in code balances exactly.
TEST_F(ComponentAssetLifetimeTest, AComponentAddedInCodeHoldsOneReference)
{
    const EntityId entity = Scene.Components.CreateEntity();

    AssetLease held = Assets.LoadLease(kClipPath, AssetType::Audio);
    ASSERT_TRUE(held.IsValid());

    AudioSourceComponent source{};
    source.Clip = AudioClipHandle::FromToken(held.OpaqueToken());
    Scene.Components.AddComponent(entity, source); // OnAdd retains

    held.Reset(); // the caller lets go of its own
    EXPECT_TRUE(Clips.IsResident(kClipPath))
        << "the component still names the clip";

    Scene.Components.DestroyEntity(entity); // OnRemove releases
    EXPECT_FALSE(Clips.IsResident(kClipPath))
        << "nothing names the clip any more";
}

// The same entity, arriving from content instead.
TEST_F(ComponentAssetLifetimeTest, ASceneLoadedComponentHoldsOneReference)
{
    const auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "AudioSource": { "clip": "asset://audio/test/tone.wav" }
                }
            }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());
    SceneSerializationContext context(Logging, &Assets);
    ASSERT_TRUE(LoadSceneJson(*parsed, Scene, Serializers, context));
    ASSERT_EQ(Scene.Components.CountComponents<AudioSourceComponent>(), 1u);
    EXPECT_TRUE(Clips.IsResident(kClipPath));

    EntityId loaded{};
    Scene.Components.ForEachComponent<AudioSourceComponent>(
        [&](EntityId entity, const AudioSourceComponent&) { loaded = entity; });
    ASSERT_TRUE(loaded.IsValid());

    Scene.Components.DestroyEntity(loaded);
    EXPECT_FALSE(Clips.IsResident(kClipPath))
        << "a clip named only by a destroyed entity is still held";
}
