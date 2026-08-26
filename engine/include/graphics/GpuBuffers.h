#pragma once

#include <graphics/BufferHandle.h>
#include <graphics/GpuResourceDesc.h>

#include <cstdint>

class VulkanBufferService;

//=============================================================================
// GpuBuffers
//
// The portable face of the backend buffer table: create, upload, destroy, by
// neutral description. A copyable value holding one backend pointer, so hosts
// hand it around like the service reference it stands in for. Methods are
// defined by the active backend (src/graphics/vulkan/GpuBuffers.cpp), which
// translates the description at its own boundary.
//
// Deliberately operations, not a mirror of the service: raw-object resolution
// (VkBuffer, mapped pointers) stays a backend privilege, which is what keeps
// every holder of this type free of the graphics API.
//=============================================================================
class GpuBuffers
{
public:
    GpuBuffers() = default;
    explicit GpuBuffers(VulkanBufferService* impl)
        : Impl(impl)
    {
    }

    [[nodiscard]] BufferHandle Create(const BufferDesc& desc);
    bool Upload(BufferHandle handle, const void* data, std::uint64_t size,
                std::uint64_t offset = 0);
    void Destroy(BufferHandle handle);

    [[nodiscard]] bool IsValid() const { return Impl != nullptr; }

private:
    VulkanBufferService* Impl = nullptr;
};
