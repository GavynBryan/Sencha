#include <assets/cook/AudioCook.h>

#include <assets/audio_clip/AudioClipSerializer.h>
#include <audio/AudioClip.h>
#include <audio/AudioClipLoader.h>

// stb_vorbis is plain C designed for single-TU inclusion; this importer is
// its only consumer. No stdio decoders — sources arrive as bytes through
// the import contract.
#define STB_VORBIS_NO_STDIO
#include <stb_vorbis.c>

#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

namespace
{
    std::string_view ExtensionOf(std::string_view path)
    {
        const size_t dot = path.rfind('.');
        return dot == std::string_view::npos ? std::string_view{} : path.substr(dot);
    }

    std::optional<AudioClip> DecodeOggBytes(std::span<const std::byte> bytes)
    {
        int channels = 0;
        int sampleRate = 0;
        short* samples = nullptr;
        const int frames = stb_vorbis_decode_memory(
            reinterpret_cast<const unsigned char*>(bytes.data()),
            static_cast<int>(bytes.size()),
            &channels, &sampleRate, &samples);
        if (frames <= 0 || samples == nullptr || channels <= 0 || channels > 255 || sampleRate <= 0)
        {
            std::free(samples);
            return std::nullopt;
        }

        AudioClip clip;
        clip.SampleRate = static_cast<uint32_t>(sampleRate);
        clip.ChannelCount = static_cast<uint8_t>(channels);
        clip.Samples.assign(samples,
                            samples + static_cast<size_t>(frames) * static_cast<size_t>(channels));
        std::free(samples);
        return clip;
    }
} // namespace

std::vector<std::string_view> AudioClipImporter::SourceExtensions() const
{
    return { ".wav", ".ogg" };
}

ImportResult AudioClipImporter::Import(const ImportInput& input, ICookOutputWriter& output)
{
    const std::string_view extension = ExtensionOf(input.SourceRelPath);

    std::optional<AudioClip> clip;
    if (extension == ".wav")
        clip = LoadAudioClipFromWavBytes(input.Bytes);
    else if (extension == ".ogg")
        clip = DecodeOggBytes(input.Bytes);
    else
        return ImportResult{ .Error = "audio import: unsupported source extension" };

    if (!clip)
        return ImportResult{ .Error = "audio import: decode failed" };

    std::vector<std::byte> sclipBytes;
    if (!WriteSclipToBytes(*clip, sclipBytes))
        return ImportResult{ .Error = "audio import: sclip serialization failed" };

    CookedArtifact artifact;
    artifact.Path = "asset://" + std::string(input.SourceRelPath);
    artifact.FileRelPath = ".cooked/" + std::string(input.SourceRelPath) + ".sclip";
    artifact.Type = AssetType::Audio;

    if (!output.WriteBytes(artifact.FileRelPath, sclipBytes))
        return ImportResult{ .Error = "audio import: artifact write failed" };

    ImportResult result;
    result.Artifacts.push_back(std::move(artifact));
    return result;
}
