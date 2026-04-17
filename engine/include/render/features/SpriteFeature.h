#pragma once

#include <render/Renderer.h>
#include <render/backend/vulkan/VulkanDescriptorCache.h>
#include <render/backend/vulkan/VulkanPipelineCache.h>
#include <render/backend/vulkan/VulkanShaderCache.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanBufferService;
class VulkanFrameScratch;
class VulkanPipelineCache;
class VulkanDescriptorCache;

//=============================================================================
// SpriteFeature
//
// Sencha's reference immediate-mode sprite batcher and the first concrete
// IRenderFeature built on top of the Renderer facade.
//
// Usage model:
//
//   FeatureRef<SpriteFeature> sprites =
//       renderer.AddFeature(std::make_unique<SpriteFeature>());
//
//   // Each frame, before renderer.DrawFrame():
//   sprites->Submit({ .Center = {x,y}, .Size = {w,h}, .Texture = slot, ... });
//   sprites->Submit({ ... });
//   renderer.DrawFrame();
//
// The feature accumulates submitted sprites into a CPU-side vector, stable-
// sorts by SortKey, uploads the whole batch through VulkanFrameScratch as a
// per-instance vertex buffer, and issues a single instanced draw. No staging,
// no retained state, no per-sprite descriptor binding.
//
// Sorting: the feature sorts by `SortKey` ascending with a stable sort, so
// sprites with equal keys retain submission order (painter's order within
// a layer). The feature has no opinion on what SortKey means -- games pack
// whatever they want into it (layer, -y, layer<<16|yBucket, etc). Default
// zero means "preserve submission order everywhere".
//
// Textures are never owned by the feature. Game code registers images with
// VulkanDescriptorCache directly and puts the returned BindlessImageIndex
// on its Sprite struct. One draw call can mix arbitrary textures; the shader
// samples the bindless array by index.
//
// Coordinate space is screen pixels, origin top-left. A camera / world-space
// layer is explicitly out of scope for this feature -- cameras will come as
// either a view-matrix push constant on a later revision or a dedicated
// CameraFeature that updates a shared per-frame UBO. This feature uses the
// frame UBO only for `InvViewport`.
//=============================================================================

class SpriteFeature : public IRenderFeature
{
public:
    SpriteFeature() = default;
    ~SpriteFeature() override = default;

    // -- IRenderFeature ----------------------------------------------------

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    void Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

    // -- Public submission API ---------------------------------------------

    // Drop everything pending without drawing. Rarely needed -- OnDraw clears
    // automatically -- but useful if game code wants to abort a frame.
    void ClearPending();

    // Pre-allocate the pending buffer so Submit() doesn't reallocate
    // during the frame. Call once after setup with an upper-bound estimate.
    void ReservePending(size_t count);

    [[nodiscard]] bool IsValid() const { return Valid; }

    // Tightly-packed GPU instance layout -- must match the vertex shader.
    // 48 bytes: two instances per 128-byte chunk, plenty dense.
    //
    // Exposed publicly so bulk submission paths (e.g. SpriteRenderSystem)
    // can write instances directly and skip the Sprite intermediate.
    struct GpuInstance
    {
        float Center[2];
        float HalfExtents[2];
        float UvMin[2];
        float UvMax[2];
        uint32_t Color;
        uint32_t TextureIndex;
        float SinRot;
        float CosRot;

        // Sort key carried alongside the GPU data for the counting-sort pass.
        // Not uploaded to the GPU -- the sort strips it before the memcpy.
        int32_t SortKey;
    };
    static_assert(sizeof(GpuInstance) == 52, "GpuInstance must stay 52 bytes");

    // Append a pre-built GpuInstance. Callers that fill GpuInstance directly
    // skip the Sprite→GpuInstance conversion in OnDraw and avoid storing the
    // larger Sprite struct in the pending buffer.
    void SubmitInstance(const GpuInstance& instance);

private:

    // Per-frame UBO. `vec2 InvViewport` lets the vertex shader go from
    // screen pixels to NDC without a matrix. Padded to 16 bytes to match
    // std140 alignment rules on the scratch-allocator side.
    struct FrameUbo
    {
        float InvViewport[2];
        float _pad[2];
    };

    bool BuildPipeline(VkFormat colorFormat);

    // Cached services (no ServiceHost lookups on the hot path).
    LoggingProvider* Logging = nullptr;
    VulkanDeviceService* DeviceService = nullptr;
    VulkanBufferService* Buffers = nullptr;
    VulkanShaderCache* Shaders = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;
    VulkanFrameScratch* Scratch = nullptr;

    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline CachedPipeline = VK_NULL_HANDLE;
    VkFormat CachedColorFormat = VK_FORMAT_UNDEFINED;

    // Pipeline state loaded from sprite.shader at Setup() time.
    // VertexShader, FragmentShader, Layout, VertexAttributes, and ColorFormats
    // are NOT included here -- BuildPipeline() fills them in per call.
    GraphicsPipelineDesc MetaDesc;

    std::vector<GpuInstance> Pending;
    bool Valid = false;
};
