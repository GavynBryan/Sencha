#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <render/MeshTypes.h>

#include <vector>

class VulkanBufferService;

//=============================================================================
// MeshService
//
// Generational free-list store for GPU meshes. Create() uploads vertex and
// index data to GPU-only buffers and returns a versioned MeshHandle. Destroy()
// releases the buffers and recycles the slot. The destructor destroys all
// live meshes, so MeshService must outlive any handles in flight.
//=============================================================================
class MeshService : public IService
{
public:
    MeshService(LoggingProvider& logging, VulkanBufferService& buffers);
    ~MeshService() override;

    MeshService(const MeshService&) = delete;
    MeshService& operator=(const MeshService&) = delete;

    [[nodiscard]] MeshHandle Create(const MeshData& data);
    void Destroy(MeshHandle handle);
    [[nodiscard]] const GpuMesh* Get(MeshHandle handle) const;

private:
    struct Entry
    {
        GpuMesh Mesh;
        uint32_t Generation = 0;
        bool Alive = false;
    };

    Logger& Log;
    VulkanBufferService* Buffers = nullptr;
    std::vector<Entry> Entries;
    std::vector<uint32_t> FreeSlots;
};
