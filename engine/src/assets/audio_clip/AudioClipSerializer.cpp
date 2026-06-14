#include <assets/audio_clip/AudioClipSerializer.h>

#include <assets/audio_clip/AudioClipFormat.h>

#include <cstring>
#include <limits>

namespace
{
    void SetError(std::string* error, const char* message)
    {
        if (error)
            *error = message;
    }

    bool IsWritableClip(const AudioClip& clip)
    {
        return clip.IsValid()
            && clip.Samples.size() % clip.ChannelCount == 0;
    }
} // namespace

bool WriteSclipToBytes(const AudioClip& clip, std::vector<std::byte>& out)
{
    if (!IsWritableClip(clip))
        return false;

    SclipFileHeader header{};
    std::memcpy(header.Magic, kSclipMagic, sizeof(header.Magic));
    header.Version = kSclipVersion;
    header.SampleRate = clip.SampleRate;
    header.ChannelCount = clip.ChannelCount;
    header.SampleCount = clip.Samples.size();
    header.HeaderSize = sizeof(SclipFileHeader);
    header.SampleDataOffset = sizeof(SclipFileHeader);

    const uint64_t sampleBytes = header.SampleCount * sizeof(int16_t);

    out.clear();
    out.resize(sizeof(SclipFileHeader) + sampleBytes);
    std::memcpy(out.data(), &header, sizeof(header));
    std::memcpy(out.data() + header.SampleDataOffset, clip.Samples.data(), sampleBytes);
    return true;
}

bool LoadSclipFromBytes(std::span<const std::byte> bytes,
                        AudioClip& out,
                        std::string* error)
{
    if (!LooksLikeSclip(bytes.data(), bytes.size()))
    {
        SetError(error, "sclip: missing or truncated header");
        return false;
    }

    SclipFileHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));

    if (header.Version != kSclipVersion)
    {
        SetError(error, "sclip: unsupported container version");
        return false;
    }
    if (header.HeaderSize != sizeof(SclipFileHeader))
    {
        SetError(error, "sclip: header size mismatch");
        return false;
    }
    if (header.SampleRate == 0
        || header.ChannelCount == 0
        || header.ChannelCount > std::numeric_limits<uint8_t>::max())
    {
        SetError(error, "sclip: invalid sample rate or channel count");
        return false;
    }
    if (header.SampleCount == 0 || header.SampleCount % header.ChannelCount != 0)
    {
        SetError(error, "sclip: sample count not a whole number of frames");
        return false;
    }

    // Compare counts, not byte products: a crafted SampleCount must not be
    // able to overflow its way past the bounds check.
    const uint64_t availableBytes = header.SampleDataOffset <= bytes.size()
        ? bytes.size() - header.SampleDataOffset
        : 0;
    if (header.SampleDataOffset < sizeof(SclipFileHeader)
        || availableBytes % sizeof(int16_t) != 0
        || header.SampleCount != availableBytes / sizeof(int16_t))
    {
        SetError(error, "sclip: sample data does not cover the container exactly");
        return false;
    }
    const uint64_t sampleBytes = availableBytes;

    out = {};
    out.SampleRate = header.SampleRate;
    out.ChannelCount = static_cast<uint8_t>(header.ChannelCount);
    out.Samples.resize(header.SampleCount);
    std::memcpy(out.Samples.data(), bytes.data() + header.SampleDataOffset, sampleBytes);
    return true;
}
