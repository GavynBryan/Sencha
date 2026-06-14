#include <assets/skeleton/SkeletonSerializer.h>

#include <cstdint>
#include <cstring>

namespace
{
    constexpr char kSskelMagic[4] = { 'S', 'S', 'K', 'L' };

    // Names are diagnostics; the cap just keeps a crafted length field from
    // asking for an absurd allocation before the bounds check would catch it.
    constexpr uint32_t kMaxJointNameLength = 4096;

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
    };
} // namespace

bool WriteSskelToBytes(const SkeletonData& skeleton,
                       std::vector<std::byte>& out,
                       std::string* error)
{
    if (!ValidateSkeletonData(skeleton, error))
        return false;

    // Writers must reject anything the reader would: a joint name longer than
    // the reader's cap would serialize to bytes that LoadSskelFromBytes then
    // refuses, producing an unreadable artifact.
    for (const SkeletonJoint& joint : skeleton.Joints)
    {
        if (joint.Name.size() > kMaxJointNameLength)
        {
            SetError(error, "sskel: joint name exceeds the maximum length");
            return false;
        }
    }

    out.clear();
    AppendBytes(out, kSskelMagic, sizeof(kSskelMagic));
    Append(out, kSskelFormatVersion);
    Append(out, static_cast<uint32_t>(skeleton.Joints.size()));
    Append(out, uint32_t{ 0 }); // reserved

    for (const SkeletonJoint& joint : skeleton.Joints)
    {
        Append(out, static_cast<uint32_t>(joint.Name.size()));
        AppendBytes(out, joint.Name.data(), joint.Name.size());
        Append(out, joint.ParentIndex);
        Append(out, joint.BindTranslation);
        Append(out, joint.BindRotation);
        Append(out, joint.BindScale);
        Append(out, joint.InverseBind);
    }

    return true;
}

bool LoadSskelFromBytes(std::span<const std::byte> bytes,
                        SkeletonData& out,
                        std::string* error)
{
    out = {};
    ByteCursor cursor{ bytes };

    char magic[4]{};
    uint32_t version = 0;
    uint32_t jointCount = 0;
    uint32_t reserved = 0;
    if (!cursor.Read(magic) || std::memcmp(magic, kSskelMagic, sizeof(magic)) != 0)
    {
        SetError(error, "sskel: missing or invalid magic");
        return false;
    }
    if (!cursor.Read(version) || version != kSskelFormatVersion)
    {
        SetError(error, "sskel: unsupported container version");
        return false;
    }
    if (!cursor.Read(jointCount) || !cursor.Read(reserved) || reserved != 0)
    {
        SetError(error, "sskel: truncated or non-zero reserved header");
        return false;
    }
    if (jointCount == 0 || jointCount > kMaxSkeletonJoints)
    {
        SetError(error, "sskel: joint count out of range");
        return false;
    }

    out.Joints.resize(jointCount);
    for (SkeletonJoint& joint : out.Joints)
    {
        uint32_t nameLength = 0;
        if (!cursor.Read(nameLength) || nameLength > kMaxJointNameLength
            || !cursor.ReadString(nameLength, joint.Name)
            || !cursor.Read(joint.ParentIndex)
            || !cursor.Read(joint.BindTranslation)
            || !cursor.Read(joint.BindRotation)
            || !cursor.Read(joint.BindScale)
            || !cursor.Read(joint.InverseBind))
        {
            SetError(error, "sskel: truncated joint data");
            out = {};
            return false;
        }
    }

    if (cursor.Offset != bytes.size())
    {
        SetError(error, "sskel: trailing bytes after joint data");
        out = {};
        return false;
    }

    if (!ValidateSkeletonData(out, error))
    {
        out = {};
        return false;
    }

    return true;
}
