#pragma once

#include <graphics/GpuResourceDesc.h>
#include <graphics/ImageHandle.h>

#include <cstdint>

class VulkanImageService;

//=============================================================================
// GpuImages
//
// The portable face of the backend image table: create, upload, destroy, by
// neutral description. A copyable value holding one backend pointer; methods
// are defined by the active backend (src/graphics/vulkan/GpuImages.cpp).
//
// Every image created here is sampled color data that can be uploaded to --
// the description says so. Views, layouts, and attachment usages stay a
// backend privilege; a pass that needs them resolves the handle through the
// backend service it already holds.
//=============================================================================
class GpuImages
{
public:
    GpuImages() = default;
    explicit GpuImages(VulkanImageService* impl)
        : Impl(impl)
    {
    }

    [[nodiscard]] ImageHandle Create(const ImageDesc& desc);
    bool Upload(ImageHandle handle, const void* data, std::uint64_t size);
    void Destroy(ImageHandle handle);

    [[nodiscard]] bool IsValid() const { return Impl != nullptr; }

private:
    VulkanImageService* Impl = nullptr;
};
