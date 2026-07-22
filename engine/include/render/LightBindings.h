#pragma once

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <render/LightGpuTypes.h>

#include <cstdint>
#include <vulkan/vulkan.h>

class VulkanUploadContextService;

//=============================================================================
// LightBindings
//
// Owns the lighting descriptor set (set 2) every forward pipeline binds:
//   binding 0: spot shadow atlas, comparison-sampled (sampler2DShadow)
//   binding 1: point shadow cube array, comparison-sampled
//              (samplerCubeArrayShadow)
//   binding 2: irradiance probe volumes,
//              sampler3D[kMaxActiveProbeVolumes * kProbeVolumeChannelCount]
//              (three SH channel textures per volume slot; dummy-filled;
//              update-after-bind so zone streaming swaps slots mid-flight)
// Development profiling builds additionally expose the same depth images
// through nearest, non-comparison samplers at bindings 3 and 4. Only the
// debug-view shader sees those bindings; shipping layouts stop at binding 2.
//
// Setup creates the layout, the set, and tiny always-valid dummy resources
// for every binding: depth dummies clear to 1.0 so comparison samples read
// fully lit, probe dummies clear to zero. CreateAtlas and CreateCubePool
// then replace bindings 0 and 1 with the real shadow targets; callers that
// only sample the set (material preview) skip them and never pay for the
// images. If creation fails, the dummy stays bound: lighting still renders
// and shadows read fully lit.
//=============================================================================
class LightBindings
{
public:
    bool Setup(const RendererServices& services);
    bool CreateAtlas();
    bool CreateCubePool();
    void Teardown();

    // Points volume slot `slot` at its three SH channel textures (R, G, B
    // coefficient volumes, 3D views in SHADER_READ_ONLY layout), or back at
    // the dummy. Binding 2 is update-after-bind: both are safe while frames
    // holding the set are in flight, as long as no in-flight frame's UBO
    // headers still reference the slot (the caller parks a slot for a frame
    // before reusing it, or relies on zone teardown's device-idle).
    void SetProbeVolume(std::uint32_t slot, VkImageView r, VkImageView g,
                        VkImageView b);
    void ResetProbeVolume(std::uint32_t slot);

    [[nodiscard]] bool IsValid() const;
    [[nodiscard]] bool HasAtlas() const { return Atlas.IsValid(); }
    [[nodiscard]] bool HasCubePool() const { return CubePool.IsValid(); }
    [[nodiscard]] VkDescriptorSetLayout GetSetLayout() const { return SetLayout; }
    [[nodiscard]] VkDescriptorSet GetSet() const { return Set; }
    [[nodiscard]] VkImage GetAtlasImage() const;
    [[nodiscard]] VkImageView GetAtlasView() const;
    [[nodiscard]] VkImage GetCubePoolImage() const;
    // 2D render view of one cube face layer.
    [[nodiscard]] VkImageView GetCubeFaceView(std::uint32_t slot,
                                              std::uint32_t face) const;

    void TransitionAtlasForWrite(VkCommandBuffer commandBuffer);
    void TransitionAtlasForRead(VkCommandBuffer commandBuffer);
    void TransitionCubePoolForWrite(VkCommandBuffer commandBuffer);
    void TransitionCubePoolForRead(VkCommandBuffer commandBuffer);

private:
    [[nodiscard]] bool CreateSamplers();
    [[nodiscard]] bool CreateDummies();
    [[nodiscard]] bool CreateSetObjects();
    void WriteBinding(std::uint32_t binding, std::uint32_t arrayElement,
                      VkSampler sampler, VkImageView view, VkImageLayout layout);
    [[nodiscard]] bool ParkDepthImage(VkImage image, std::uint32_t layerCount);
    void DestroyCubeFaceViews();

    VulkanImageService* Images = nullptr;
    VulkanUploadContextService* Upload = nullptr;
    VkDevice Device = VK_NULL_HANDLE;

    ImageHandle Atlas;
    ImageHandle CubePool;          // kPointShadowFaceExtent D16 cube array
    ImageHandle DummyShadowMap;    // 1x1 D16, cleared to 1.0
    ImageHandle DummyShadowCube;   // 1x1, six-layer D16 cube array, cleared to 1.0
    ImageHandle DummyProbeVolume;  // 1x1x1 RGBA16F 3D, cleared to zero

    VkImageView CubeFaceViews[kMaxPointShadows * kPointShadowFaceCount] = {};

    VkSampler ShadowSampler = VK_NULL_HANDLE;       // comparison, border-clamped; binding 0
    VkSampler PointShadowSampler = VK_NULL_HANDLE;  // comparison, edge-clamped; binding 1
    VkSampler ProbeSampler = VK_NULL_HANDLE;        // linear; binding 2
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    VkSampler RawShadowSampler = VK_NULL_HANDLE;       // nearest depth; binding 3
    VkSampler RawPointShadowSampler = VK_NULL_HANDLE;  // nearest cube depth; binding 4
#endif
    VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    VkDescriptorSet Set = VK_NULL_HANDLE;
    VkImageLayout AtlasLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout CubeLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};
