#include <assets/animation/AnimationClipSerializer.h>

#include <cstdint>
#include <cstring>

namespace
{
    constexpr char kSanimMagic[4] = { 'S', 'A', 'N', 'M' };

    // Generous sanity caps so a crafted count field cannot demand an absurd
    // allocation before the bounds check would catch it.
    constexpr uint32_t kMaxTrackCount = 65536;
    constexpr uint32_t kMaxKeyCount = 1u << 24;
    constexpr uint32_t kMaxSkeletonPathLength = 4096;

    void SetError(std::string* error, const char* message)
    {
        if (error)
            *error = message;
    }

    template <typename T>
    void Append(std::vector<std::byte>& out, const T& value)
    {
        const size_t offset = out.size();
        out.resize(offset + sizeof(T));
        std::memcpy(out.data() + offset, &value, sizeof(T));
    }

    void AppendBytes(std::vector<std::byte>& out, const void* data, size_t size)
    {
        const size_t offset = out.size();
        out.resize(offset + size);
        if (size > 0)
            std::memcpy(out.data() + offset, data, size);
    }

    struct ByteCursor
    {
        std::span<const std::byte> Bytes;
        size_t Offset = 0;

        template <typename T>
        bool Read(T& out)
        {
            if (Bytes.size() - Offset < sizeof(T))
                return false;
            std::memcpy(&out, Bytes.data() + Offset, sizeof(T));
            Offset += sizeof(T);
            return true;
        }

        bool ReadString(uint32_t length, std::string& out)
        {
            if (Bytes.size() - Offset < length)
                return false;
            out.assign(reinterpret_cast<const char*>(Bytes.data() + Offset), length);
            Offset += length;
            return true;
        }

        bool ReadFloats(size_t count, std::vector<float>& out)
        {
            const size_t byteCount = count * sizeof(float);
            if (count > Bytes.size() || Bytes.size() - Offset < byteCount)
                return false;
            out.resize(count);
            if (count > 0)
                std::memcpy(out.data(), Bytes.data() + Offset, byteCount);
            Offset += byteCount;
            return true;
        }
    };
} // namespace

bool WriteSanimToBytes(const AnimationClipData& clip,
                       std::vector<std::byte>& out,
                       std::string* error)
{
    if (!ValidateAnimationClipData(clip, error))
        return false;

    // Writers must reject anything the reader would, so a written artifact is
    // always loadable: the reader caps the skeleton path length, track count,
    // and per-track key count, so the writer enforces the same bounds.
    if (clip.SkeletonPath.size() > kMaxSkeletonPathLength)
    {
        SetError(error, "sanim: skeleton path too long");
        return false;
    }
    if (clip.Tracks.size() > kMaxTrackCount)
    {
        SetError(error, "sanim: track count exceeds the maximum");
        return false;
    }
    for (const AnimationJointTrack& track : clip.Tracks)
    {
        if (track.TimesSeconds.size() > kMaxKeyCount)
        {
            SetError(error, "sanim: track key count exceeds the maximum");
            return false;
        }
    }

    out.clear();
    AppendBytes(out, kSanimMagic, sizeof(kSanimMagic));
    Append(out, kSanimFormatVersion);
    Append(out, static_cast<uint32_t>(clip.Tracks.size()));
    Append(out, clip.DurationSeconds);
    Append(out, static_cast<uint32_t>(clip.SkeletonPath.size()));
    Append(out, uint32_t{ 0 }); // reserved
    AppendBytes(out, clip.SkeletonPath.data(), clip.SkeletonPath.size());

    for (const AnimationJointTrack& track : clip.Tracks)
    {
        Append(out, track.JointIndex);
        Append(out, static_cast<uint32_t>(track.Path));
        Append(out, static_cast<uint32_t>(track.Interpolation));
        Append(out, static_cast<uint32_t>(track.TimesSeconds.size()));
        AppendBytes(out, track.TimesSeconds.data(),
                    track.TimesSeconds.size() * sizeof(float));
        AppendBytes(out, track.Values.data(), track.Values.size() * sizeof(float));
    }

    return true;
}

bool LoadSanimFromBytes(std::span<const std::byte> bytes,
                        AnimationClipData& out,
                        std::string* error)
{
    out = {};
    ByteCursor cursor{ bytes };

    char magic[4]{};
    uint32_t version = 0;
    uint32_t trackCount = 0;
    uint32_t skeletonPathLength = 0;
    uint32_t reserved = 0;
    if (!cursor.Read(magic) || std::memcmp(magic, kSanimMagic, sizeof(magic)) != 0)
    {
        SetError(error, "sanim: missing or invalid magic");
        return false;
    }
    if (!cursor.Read(version) || version != kSanimFormatVersion)
    {
        SetError(error, "sanim: unsupported container version");
        return false;
    }
    if (!cursor.Read(trackCount) || !cursor.Read(out.DurationSeconds)
        || !cursor.Read(skeletonPathLength) || !cursor.Read(reserved) || reserved != 0)
    {
        SetError(error, "sanim: truncated or non-zero reserved header");
        return false;
    }
    if (trackCount == 0 || trackCount > kMaxTrackCount
        || skeletonPathLength == 0 || skeletonPathLength > kMaxSkeletonPathLength)
    {
        SetError(error, "sanim: track count or skeleton path length out of range");
        return false;
    }
    if (!cursor.ReadString(skeletonPathLength, out.SkeletonPath))
    {
        SetError(error, "sanim: truncated skeleton path");
        return false;
    }

    out.Tracks.resize(trackCount);
    for (AnimationJointTrack& track : out.Tracks)
    {
        uint32_t path = 0;
        uint32_t interpolation = 0;
        uint32_t keyCount = 0;
        if (!cursor.Read(track.JointIndex) || !cursor.Read(path)
            || !cursor.Read(interpolation) || !cursor.Read(keyCount))
        {
            SetError(error, "sanim: truncated track header");
            out = {};
            return false;
        }
        if (path > static_cast<uint32_t>(AnimationChannelPath::Scale)
            || interpolation > static_cast<uint32_t>(AnimationInterpolation::Step)
            || keyCount == 0 || keyCount > kMaxKeyCount)
        {
            SetError(error, "sanim: track header values out of range");
            out = {};
            return false;
        }
        track.Path = static_cast<AnimationChannelPath>(path);
        track.Interpolation = static_cast<AnimationInterpolation>(interpolation);

        const uint32_t components = AnimationChannelComponentCount(track.Path);
        if (!cursor.ReadFloats(keyCount, track.TimesSeconds)
            || !cursor.ReadFloats(size_t{ keyCount } * components, track.Values))
        {
            SetError(error, "sanim: truncated track data");
            out = {};
            return false;
        }
    }

    if (cursor.Offset != bytes.size())
    {
        SetError(error, "sanim: trailing bytes after track data");
        out = {};
        return false;
    }

    if (!ValidateAnimationClipData(out, error))
    {
        out = {};
        return false;
    }

    return true;
}
