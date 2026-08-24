#pragma once

#include <graphics/RenderExtent.h>

#include <cstdint>

//=============================================================================
// GPU resource descriptions, backend-neutral.
//
// The vocabulary for creating device buffers and images without naming a
// graphics API: render-domain code fills one of these and hands it to the
// GpuBuffers / GpuImages surface, and the active backend translates at its
// own boundary. Every enum here grows only on demand -- a value with no
// caller is a value that cannot be tested.
//=============================================================================

// Where a buffer's memory lives and how the CPU may touch it.
enum class BufferMemory : std::uint8_t
{
    // Device-local, no host visibility. Upload stages through a transient
    // host-visible buffer.
    GpuOnly,
    // Persistently mapped, sequential-write friendly. Upload is a memcpy.
    HostVisible,
    // Host-visible, random-access, for GPU -> CPU traffic.
    Readback,
};

// How a buffer will be bound. Combine with |.
enum class GpuBufferUsage : std::uint8_t
{
    None = 0,
    Vertex = 1 << 0,
    Index = 1 << 1,
    Storage = 1 << 2,
};

[[nodiscard]] constexpr GpuBufferUsage operator|(GpuBufferUsage a, GpuBufferUsage b)
{
    return static_cast<GpuBufferUsage>(static_cast<std::uint8_t>(a)
                                       | static_cast<std::uint8_t>(b));
}

[[nodiscard]] constexpr bool HasUsage(GpuBufferUsage usage, GpuBufferUsage flag)
{
    return (static_cast<std::uint8_t>(usage) & static_cast<std::uint8_t>(flag)) != 0;
}

struct BufferDesc
{
    std::uint64_t Size = 0;
    GpuBufferUsage Usage = GpuBufferUsage::None;
    BufferMemory Memory = BufferMemory::GpuOnly;
    const char* DebugName = nullptr;
};

// Pixel formats the neutral surface can request.
enum class GpuFormat : std::uint8_t
{
    Rgba16Float,
};

// Image dimensionality, driven by the view the image is sampled through.
enum class GpuImageViewKind : std::uint8_t
{
    Image2D,
    Volume,
};

// Texel filtering within and across mip levels.
enum class SamplerFilter : std::uint8_t
{
    Linear,
    Nearest,
};

// What sampling outside [0,1) reads.
enum class SamplerAddress : std::uint8_t
{
    Repeat,
    ClampToEdge,
};

// MaxLod value meaning "never clamp the mip chain". Large enough for any
// real chain; backends with an explicit no-clamp constant map it there.
inline constexpr float kSamplerLodUnclamped = 1000.0f;

// How an image is sampled. Samplers are pure immutable state, deduplicated
// by the backend sampler cache; describe what you want and share the result.
struct SamplerDesc
{
    SamplerFilter MinFilter = SamplerFilter::Linear;
    SamplerFilter MagFilter = SamplerFilter::Linear;
    SamplerFilter MipmapMode = SamplerFilter::Linear;
    SamplerAddress AddressModeU = SamplerAddress::Repeat;
    SamplerAddress AddressModeV = SamplerAddress::Repeat;
    SamplerAddress AddressModeW = SamplerAddress::Repeat;
    float MaxAnisotropy = 0.0f; // 0 disables anisotropy
    float MaxLod = kSamplerLodUnclamped;

    bool operator==(const SamplerDesc&) const = default;
};

// A sampled image. Every image created through this description is
// shader-sampled color data that can be uploaded to; attachments and other
// specialized usages are backend territory (render targets go through
// RenderTargetStore).
struct ImageDesc
{
    GpuFormat Format = GpuFormat::Rgba16Float;
    RenderExtent Extent{};
    // Volume only: slice count.
    std::uint32_t Depth = 1;
    GpuImageViewKind ViewKind = GpuImageViewKind::Image2D;
    const char* DebugName = nullptr;
};
