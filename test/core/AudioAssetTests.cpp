#include <assets/audio_clip/AudioClipAssetLoader.h>
#include <assets/audio_clip/AudioClipFormat.h>
#include <assets/audio_clip/AudioClipSerializer.h>
#include <audio/AudioClipCache.h>
#include <audio/AudioClipLoader.h>
#include <core/assets/AssetSource.h>
#include <core/assets/AssetSystem.h>
#include <core/logging/LoggingProvider.h>

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/AudioCook.h>
#include <assets/cook/ImportOnDemand.h>
#include "AudioOggFixture.h"
#endif

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace
{
    AudioClip MakeRampClip(uint32_t sampleRate = 44100, uint8_t channels = 2, uint32_t frames = 64)
    {
        AudioClip clip;
        clip.SampleRate = sampleRate;
        clip.ChannelCount = channels;
        clip.Samples.resize(static_cast<size_t>(frames) * channels);
        for (size_t i = 0; i < clip.Samples.size(); ++i)
            clip.Samples[i] = static_cast<int16_t>(int(i) * 7 - 100);
        return clip;
    }

    // Minimal PCM16 RIFF/WAVE writer — enough for SDL to decode, byte-exact
    // on the way back out because no format conversion is involved.
    std::vector<std::byte> MakeWavBytes(uint32_t sampleRate, uint16_t channels,
                                        const std::vector<int16_t>& samples)
    {
        const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
        const uint16_t blockAlign = static_cast<uint16_t>(channels * sizeof(int16_t));
        const uint32_t byteRate = sampleRate * blockAlign;

        std::vector<std::byte> out(44 + dataBytes);
        auto* p = reinterpret_cast<uint8_t*>(out.data());
        auto put32 = [&p](uint32_t v) { std::memcpy(p, &v, 4); p += 4; };
        auto put16 = [&p](uint16_t v) { std::memcpy(p, &v, 2); p += 2; };
        auto putTag = [&p](const char* tag) { std::memcpy(p, tag, 4); p += 4; };

        putTag("RIFF"); put32(36 + dataBytes); putTag("WAVE");
        putTag("fmt "); put32(16);
        put16(1); // PCM
        put16(channels); put32(sampleRate); put32(byteRate);
        put16(blockAlign); put16(16);
        putTag("data"); put32(dataBytes);
        std::memcpy(p, samples.data(), dataBytes);
        return out;
    }

    class MemoryAssetSource final : public IAssetSource
    {
    public:
        bool ReadBytes(std::string_view filePath, std::vector<std::byte>& out) override
        {
            auto it = Files.find(std::string(filePath));
            if (it == Files.end())
                return false;
            out = it->second;
            return true;
        }

        void Add(std::string_view path, std::vector<std::byte> bytes)
        {
            Files[std::string(path)] = std::move(bytes);
        }

    private:
        std::map<std::string, std::vector<std::byte>> Files;
    };

    AssetRecord MakeAudioRecord(std::string_view path)
    {
        return AssetRecord{
            .Type = AssetType::Audio,
            .SourceKind = AssetSourceKind::File,
            .Path = std::string(path),
            .FilePath = std::string(path),
        };
    }
} // namespace

// -- .sclip round trip ------------------------------------------------------------

TEST(SclipRoundTrip, ClipSurvivesByteExact)
{
    const AudioClip clip = MakeRampClip(48000, 2, 128);

    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSclipToBytes(clip, bytes));
    ASSERT_TRUE(LooksLikeSclip(bytes.data(), bytes.size()));

    AudioClip parsed;
    std::string error;
    ASSERT_TRUE(LoadSclipFromBytes(bytes, parsed, &error)) << error;
    EXPECT_EQ(parsed.SampleRate, clip.SampleRate);
    EXPECT_EQ(parsed.ChannelCount, clip.ChannelCount);
    EXPECT_EQ(parsed.Samples, clip.Samples);
}

TEST(SclipRoundTrip, WriteRejectsInvalidClips)
{
    std::vector<std::byte> bytes;

    AudioClip empty;
    EXPECT_FALSE(WriteSclipToBytes(empty, bytes));

    // Sample count not a whole number of frames.
    AudioClip torn = MakeRampClip(44100, 2, 4);
    torn.Samples.pop_back();
    EXPECT_FALSE(WriteSclipToBytes(torn, bytes));
}

TEST(SclipRoundTrip, RejectsCorruptContainers)
{
    const AudioClip clip = MakeRampClip();
    std::vector<std::byte> good;
    ASSERT_TRUE(WriteSclipToBytes(clip, good));

    AudioClip parsed;

    // Truncated past the header.
    std::vector<std::byte> truncated(good.begin(), good.begin() + good.size() / 2);
    EXPECT_FALSE(LoadSclipFromBytes(truncated, parsed));

    // Bad magic.
    std::vector<std::byte> badMagic = good;
    badMagic[0] = std::byte{'X'};
    EXPECT_FALSE(LoadSclipFromBytes(badMagic, parsed));

    // Unknown version.
    std::vector<std::byte> badVersion = good;
    const uint32_t version = 999;
    std::memcpy(badVersion.data() + 4, &version, sizeof(version));
    EXPECT_FALSE(LoadSclipFromBytes(badVersion, parsed));

    // Header sample count disagrees with the payload.
    std::vector<std::byte> lyingCount = good;
    const uint64_t wrongCount = clip.Samples.size() + 2;
    std::memcpy(lyingCount.data() + 16, &wrongCount, sizeof(wrongCount));
    EXPECT_FALSE(LoadSclipFromBytes(lyingCount, parsed));

    // Trailing garbage.
    std::vector<std::byte> trailing = good;
    trailing.push_back(std::byte{0});
    trailing.push_back(std::byte{0});
    EXPECT_FALSE(LoadSclipFromBytes(trailing, parsed));
}

// -- WAV decode (bytes in, clip out) ------------------------------------------------

TEST(AudioClipWav, Pcm16BytesDecodeSampleExact)
{
    std::vector<int16_t> samples(96);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<int16_t>(int(i) * 31 - 1000);

    const auto wav = MakeWavBytes(22050, 1, samples);
    const auto clip = LoadAudioClipFromWavBytes(wav);
    ASSERT_TRUE(clip.has_value());
    EXPECT_EQ(clip->SampleRate, 22050u);
    EXPECT_EQ(clip->ChannelCount, 1u);
    EXPECT_EQ(clip->Samples, samples);
}

TEST(AudioClipWav, GarbageBytesFailCleanly)
{
    const std::string garbage = "definitely not a wav file";
    const auto* data = reinterpret_cast<const std::byte*>(garbage.data());
    EXPECT_FALSE(LoadAudioClipFromWavBytes({ data, garbage.size() }).has_value());
}

// -- AudioClipCache: no IO, register/find/release -----------------------------------

TEST(AudioClipCacheContract, AcquireNeverLoadsFromDisk)
{
    LoggingProvider logging;
    AudioClipCache cache(logging);

    // A path that exists on no filesystem and a path-shaped string both
    // miss: the cache resolves registered entries only (Decision I).
    EXPECT_FALSE(cache.Acquire("asset://audio/never_registered.wav").IsValid());
    EXPECT_FALSE(cache.Find("asset://audio/never_registered.wav").IsValid());
}

TEST(AudioClipCacheContract, RegisterDedupsAndReleaseFrees)
{
    LoggingProvider logging;
    AudioClipCache cache(logging);

    AudioClipHandle first = cache.Register("asset://audio/boop.wav", MakeRampClip());
    ASSERT_TRUE(first.IsValid());
    EXPECT_EQ(cache.GetName(first), "asset://audio/boop.wav");

    // Same path: same handle, second reference; the new clip is discarded.
    AudioClipHandle second = cache.Register("asset://audio/boop.wav", MakeRampClip(8000, 1, 8));
    EXPECT_EQ(first, second);
    EXPECT_EQ(cache.Get(second)->SampleRate, 44100u);

    cache.Release(first);
    EXPECT_NE(cache.Get(second), nullptr);
    cache.Release(second);
    EXPECT_EQ(cache.Get(second), nullptr);
    EXPECT_FALSE(cache.Find("asset://audio/boop.wav").IsValid());
}

// -- AudioClipAssetLoader: the staged round trip, fully headless ---------------------

TEST(AudioClipAssetLoaderStaged, SniffsSclipOverExtensionAndCommits)
{
    LoggingProvider logging;
    AudioClipCache cache(logging);
    AudioClipAssetLoader loader(logging, &cache);

    // .sclip bytes registered under a ".wav" virtual path — exactly what a
    // cooked artifact looks like (the artifact keeps the source's path).
    const AudioClip clip = MakeRampClip(32000, 1, 32);
    std::vector<std::byte> sclipBytes;
    ASSERT_TRUE(WriteSclipToBytes(clip, sclipBytes));

    MemoryAssetSource source;
    source.Add("asset://audio/cooked.wav", std::move(sclipBytes));

    AssetStaging staging = loader.LoadStaged(MakeAudioRecord("asset://audio/cooked.wav"), source);
    ASSERT_TRUE(staging.IsValid()) << staging.Error;

    AudioClipHandle handle = loader.CommitTyped(std::move(staging));
    ASSERT_TRUE(handle.IsValid());

    const AudioClip* resident = cache.Get(handle);
    ASSERT_NE(resident, nullptr);
    EXPECT_EQ(resident->SampleRate, 32000u);
    EXPECT_EQ(resident->Samples, clip.Samples);

    cache.Release(handle);
    EXPECT_FALSE(cache.Find("asset://audio/cooked.wav").IsValid());
}

TEST(AudioClipAssetLoaderStaged, LooseWavBytesStageThroughTheFallback)
{
    LoggingProvider logging;
    AudioClipAssetLoader loader(logging, nullptr);

    std::vector<int16_t> samples(48, 1234);
    MemoryAssetSource source;
    source.Add("asset://audio/loose.wav", MakeWavBytes(44100, 2, samples));

    AssetStaging staging = loader.LoadStaged(MakeAudioRecord("asset://audio/loose.wav"), source);
    ASSERT_TRUE(staging.IsValid()) << staging.Error;

    const auto* clip = std::any_cast<AudioClip>(&staging.Payload);
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(clip->ChannelCount, 2u);
    EXPECT_EQ(clip->Samples, samples);

    // No cache: commit fails cleanly.
    EXPECT_FALSE(loader.CommitTyped(std::move(staging)).IsValid());
}

TEST(AudioClipAssetLoaderStaged, MalformedBytesFailStagingWithError)
{
    LoggingProvider logging;
    AudioClipAssetLoader loader(logging, nullptr);

    MemoryAssetSource source;
    source.Add("asset://audio/garbage.wav", MakeWavBytes(44100, 1, { 1, 2, 3, 4 }));

    // Missing source.
    AssetStaging missing = loader.LoadStaged(MakeAudioRecord("asset://audio/absent.wav"), source);
    EXPECT_FALSE(missing.IsValid());

    // Corrupt .sclip: truncated past the magic.
    const AudioClip clip = MakeRampClip();
    std::vector<std::byte> torn;
    ASSERT_TRUE(WriteSclipToBytes(clip, torn));
    torn.resize(torn.size() - 1);
    source.Add("asset://audio/torn.sclip", std::move(torn));

    AssetStaging staging = loader.LoadStaged(MakeAudioRecord("asset://audio/torn.sclip"), source);
    EXPECT_FALSE(staging.IsValid());
    EXPECT_NE(staging.Error.find("sclip"), std::string::npos);
}

TEST(AudioClipAssetLoaderStaged, CommitRejectsForeignPayloadType)
{
    LoggingProvider logging;
    AudioClipCache cache(logging);
    AudioClipAssetLoader loader(logging, &cache);

    AssetStaging staging;
    staging.Record = MakeAudioRecord("asset://audio/wrong.wav");
    staging.Payload = std::string("not an AudioClip");

    EXPECT_FALSE(loader.CommitTyped(std::move(staging)).IsValid());
    EXPECT_FALSE(cache.Find("asset://audio/wrong.wav").IsValid());
}

// -- AssetSystem front door: sync path = LoadStaged + Commit back-to-back ------------

namespace
{
    class TempSclipAsset
    {
    public:
        TempSclipAsset(AssetRegistry& registry, std::string_view name, const AudioClip& clip)
        {
            std::vector<std::byte> bytes;
            EXPECT_TRUE(WriteSclipToBytes(clip, bytes));

            static int counter = 0;
            File = std::filesystem::temp_directory_path() /
                   ("sencha_audio_asset_" + std::to_string(++counter) + ".sclip");
            std::ofstream out(File, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));

            Path = "asset://audio/test/" + std::string(name) + ".sclip";
            EXPECT_TRUE(registry.Register(AssetRecord{
                .Type = AssetType::Audio,
                .SourceKind = AssetSourceKind::File,
                .Path = Path,
                .FilePath = File.generic_string(),
            }));
        }

        ~TempSclipAsset()
        {
            std::error_code ec;
            std::filesystem::remove(File, ec);
        }

        std::string Path;
        std::filesystem::path File;
    };
}

TEST(AssetSystemAudio, LoadAudioClipResolvesDedupsAndReleases)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    AudioClipCache cache(logging);
    AssetSystem assets(logging, registry, nullptr, nullptr, nullptr, &cache);

    const AudioClip clip = MakeRampClip(11025, 1, 16);
    TempSclipAsset blip(registry, "blip", clip);

    // Not resident yet: TryAcquire never loads.
    EXPECT_FALSE(assets.TryAcquireAudioClip(blip.Path).IsValid());

    AudioClipHandle first = assets.LoadAudioClip(blip.Path);
    ASSERT_TRUE(first.IsValid());
    ASSERT_NE(cache.Get(first), nullptr);
    EXPECT_EQ(cache.Get(first)->Samples, clip.Samples);

    // Second load dedups onto the same entry.
    AudioClipHandle second = assets.LoadAudioClip(blip.Path);
    EXPECT_EQ(first, second);

    // Resident now: TryAcquire takes a third reference.
    AudioClipHandle third = assets.TryAcquireAudioClip(blip.Path);
    EXPECT_EQ(first, third);

    assets.ReleaseAudioClip(first);
    assets.ReleaseAudioClip(second);
    EXPECT_NE(cache.Get(third), nullptr);
    assets.ReleaseAudioClip(third);
    EXPECT_EQ(cache.Get(third), nullptr);
}

TEST(AssetSystemAudio, WrongTypeAndMissingRecordFailCleanly)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    AudioClipCache cache(logging);
    AssetSystem assets(logging, registry, nullptr, nullptr, nullptr, &cache);

    EXPECT_FALSE(assets.LoadAudioClip("asset://audio/unregistered.sclip").IsValid());

    ASSERT_TRUE(registry.Register(AssetRecord{
        .Type = AssetType::Material,
        .SourceKind = AssetSourceKind::File,
        .Path = "asset://materials/red.smat",
        .FilePath = "red.smat",
    }));
    EXPECT_FALSE(assets.LoadAudioClip("asset://materials/red.smat").IsValid());
}

#ifdef SENCHA_ENABLE_COOK

// -- WAV/OGG -> .sclip, end to end through import-on-demand --------------------------

namespace
{
    class TempAudioRoot
    {
    public:
        TempAudioRoot()
        {
            std::random_device rd;
            Root = std::filesystem::temp_directory_path()
                 / ("sencha_audio_import_test_" + std::to_string(rd()));
            std::filesystem::create_directories(Root / "audio");
        }

        ~TempAudioRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        void WriteFile(const std::filesystem::path& relPath, std::span<const std::byte> bytes)
        {
            std::ofstream out(Root / relPath, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }

        std::filesystem::path Root;
    };
}

TEST(AudioClipImport, WavCooksToSclipEndToEnd)
{
    TempAudioRoot root;
    std::vector<int16_t> samples(256);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<int16_t>(int(i) * 100 - 12800);
    root.WriteFile("audio/blip.wav", MakeWavBytes(44100, 2, samples));

    LoggingProvider logging;
    AssetRegistry registry(logging);
    AudioClipImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));

    ImportOnDemandStats stats;
    ASSERT_TRUE(ImportAssetsOnDemand(root.Root.generic_string(), importers, registry,
                                     logging, &stats));
    EXPECT_EQ(stats.SourcesSeen, 1u);
    EXPECT_EQ(stats.Imported, 1u);
    EXPECT_EQ(stats.Failed, 0u);

    // The artifact registered under the source's virtual path.
    const AssetRecord* record = registry.FindByPath("asset://audio/blip.wav");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Type, AssetType::Audio);
    EXPECT_TRUE(record->FilePath.ends_with(".cooked/audio/blip.wav.sclip"));

    // The artifact is a well-formed .sclip carrying the source PCM exactly.
    FileAssetSource files;
    std::vector<std::byte> bytes;
    ASSERT_TRUE(files.ReadBytes(record->FilePath, bytes));
    ASSERT_TRUE(LooksLikeSclip(bytes.data(), bytes.size()));

    AudioClip cooked;
    std::string error;
    ASSERT_TRUE(LoadSclipFromBytes(bytes, cooked, &error)) << error;
    EXPECT_EQ(cooked.SampleRate, 44100u);
    EXPECT_EQ(cooked.ChannelCount, 2u);
    EXPECT_EQ(cooked.Samples, samples);

    // Warm cache: the importer is not invoked again.
    AssetRegistry registry2(logging);
    ImportOnDemandStats stats2;
    ASSERT_TRUE(ImportAssetsOnDemand(root.Root.generic_string(), importers, registry2,
                                     logging, &stats2));
    EXPECT_EQ(stats2.CookedFresh, 1u);
    EXPECT_EQ(stats2.Imported, 0u);
    EXPECT_TRUE(registry2.Contains("asset://audio/blip.wav"));
}

TEST(AudioClipImport, OggDecodesThroughStbVorbis)
{
    // The committed fixture: 1024 frames of a 440 Hz sine, mono, 22050 Hz.
    // Vorbis is lossy — assert on rate/channels/duration and rough waveform
    // shape, never exact samples.
    TempAudioRoot root;
    root.WriteFile("audio/sine.ogg",
                   std::as_bytes(std::span(kSineOggFixture, sizeof(kSineOggFixture))));

    LoggingProvider logging;
    AssetRegistry registry(logging);
    AudioClipImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));
    ASSERT_TRUE(ImportAssetsOnDemand(root.Root.generic_string(), importers, registry, logging));

    const AssetRecord* record = registry.FindByPath("asset://audio/sine.ogg");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Type, AssetType::Audio);

    FileAssetSource files;
    std::vector<std::byte> bytes;
    ASSERT_TRUE(files.ReadBytes(record->FilePath, bytes));

    AudioClip cooked;
    std::string error;
    ASSERT_TRUE(LoadSclipFromBytes(bytes, cooked, &error)) << error;
    EXPECT_EQ(cooked.SampleRate, 22050u);
    EXPECT_EQ(cooked.ChannelCount, 1u);
    EXPECT_EQ(cooked.FrameCount(), 1024u);

    // A 12000-amplitude sine survives lossy encode recognizably.
    int16_t peak = 0;
    for (int16_t s : cooked.Samples)
        peak = std::max<int16_t>(peak, static_cast<int16_t>(std::abs(int(s))));
    EXPECT_GT(peak, 8000);
    EXPECT_LE(peak, 16000);
}

TEST(AudioClipImport, GarbageSourceFailsWithoutArtifacts)
{
    TempAudioRoot root;
    const std::string garbage = "not audio";
    root.WriteFile("audio/broken.ogg",
                   std::as_bytes(std::span(garbage.data(), garbage.size())));

    LoggingProvider logging;
    AssetRegistry registry(logging);
    AudioClipImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));

    ImportOnDemandStats stats;
    EXPECT_FALSE(ImportAssetsOnDemand(root.Root.generic_string(), importers, registry,
                                      logging, &stats));
    EXPECT_EQ(stats.Failed, 1u);
    EXPECT_FALSE(registry.Contains("asset://audio/broken.ogg"));
}

#endif // SENCHA_ENABLE_COOK
