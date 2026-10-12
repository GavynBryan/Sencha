#pragma once

#include <graphics/GpuFrameRetirement.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <render/ui/UiDrawFrame.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

struct RendererServices;

// The GPU side of every texture the authored UI generated, for every pass that
// draws it: one per renderer, because a glyph atlas belongs to a font, not to a
// surface (docs/ui/architecture.md §9).
class UiTextureResidency
{
public:
    [[nodiscard]] bool Setup(const RendererServices& services);
    void Teardown();

    // What one extraction minted and released. Queued, not applied: the frame
    // that will draw them has not started recording yet.
    void Queue(UiTextureChanges&& changes);

    struct Applied
    {
        std::uint32_t Uploads = 0;
        std::uint64_t UploadBytes = 0;
    };

    // Applies everything queued and frees what has retired. Any pass may call
    // it before drawing; after the first call in a frame there is nothing left
    // to apply and it returns zeros.
    Applied Prepare(const GpuFrameRetirement& clock);

    [[nodiscard]] std::optional<BindlessImageIndex> FindGenerated(UiGeneratedTextureId id) const;

    // What stands behind a host image this frame, published before the UI draws
    // and kept alive by the host while published; an image nothing is published
    // under draws untextured.
    void PublishHostImage(UiHostImageId id, BindlessImageIndex image);
    void WithdrawHostImage(UiHostImageId id);
    [[nodiscard]] std::optional<BindlessImageIndex> FindHost(UiHostImageId id) const;

    [[nodiscard]] std::size_t ResidentCount() const { return Generated.size(); }

private:
    struct GeneratedTexture
    {
        ImageHandle Image{};
        BindlessImageIndex Bindless{};
    };

    // Kept alive until the frames that referenced them have retired. A texture
    // released mid-flight is still named by command buffers not yet finished.
    struct RetiredTexture
    {
        GeneratedTexture Texture;
        std::uint64_t RetireStamp = 0;
    };

    void Destroy(const GeneratedTexture& texture);

    const RendererServices* Services = nullptr;
    VkSampler Sampler = VK_NULL_HANDLE;
    UiTextureChanges Pending;
    std::unordered_map<UiGeneratedTextureId, GeneratedTexture> Generated;
    std::vector<RetiredTexture> Retiring;
    std::unordered_map<UiHostImageId, BindlessImageIndex> HostImages;
};
