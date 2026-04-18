#pragma once

#include <core/service/IService.h>
#include <math/Vec.h>

#include <cstdint>
#include <vector>

// Versioned handle to a material owned by MaterialStore. Generation 0 is null.
struct MaterialHandle
{
    uint32_t Index = UINT32_MAX;
    uint32_t Generation = 0;

    [[nodiscard]] bool IsValid() const { return Index != UINT32_MAX && Generation != 0; }
    bool operator==(const MaterialHandle&) const = default;
};

// Identifies the render pass a material belongs to. Used as the high bits of the sort key.
enum class ShaderPassId : uint16_t
{
    ForwardOpaque = 0
};

//=============================================================================
// Material
//
// CPU-side material descriptor. Specifies which shader pass to use and
// surface parameters. Owned and versioned by MaterialStore; accessed via
// MaterialHandle.
//
// BaseColorTextureIndex == UINT32_MAX means no texture; shaders treat it as
// white and multiply by BaseColor.
//=============================================================================
struct Material
{
    ShaderPassId Pass = ShaderPassId::ForwardOpaque;
    Vec4 BaseColor = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    uint32_t BaseColorTextureIndex = UINT32_MAX;
};

//=============================================================================
// MaterialStore
//
// Generational free-list store for Material objects. Create() returns a
// versioned handle; Destroy() recycles the slot and bumps the generation so
// stale handles return nullptr from Get().
//=============================================================================
class MaterialStore : public IService
{
public:
    MaterialHandle Create(const Material& material);
    void Destroy(MaterialHandle handle);
    [[nodiscard]] const Material* Get(MaterialHandle handle) const;

private:
    struct Entry
    {
        Material Value{};
        uint32_t Generation = 0;
        bool Alive = false;
    };

    std::vector<Entry> Entries;
    std::vector<uint32_t> FreeSlots;
};
