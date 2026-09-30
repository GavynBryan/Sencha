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

//=============================================================================
// UiTextureResidency
//
// The GPU side of every texture the authored UI generated -- glyph atlases,
// decorator images -- for every pass that draws authored UI. One per renderer.
//
// One owner rather than one per pass because a generated texture belongs to the
// document engine, not to a surface: a font's atlas is minted while whichever
// surface first needs it records, and every other surface, drawn by whichever
// pass draws it (the window's, a host's offscreen one), names the same id.
//
// Changes arrive once per extraction and are queued; the first UI draw of a
// frame applies everything queued, so an upload exists before any pass looks
// it up and a release is stamped against the frame clock that will retire it.
//=============================================================================
class UiTextureResidency
{
public:
    [[nodiscard]] bool Setup(const RendererServices& services);
    void Teardown();

    // What one extraction minted and released. Queued, not applied: the frame
    // that will draw them has not started recording yet.
    void Queue(const UiTextureChanges& changes);

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
};
