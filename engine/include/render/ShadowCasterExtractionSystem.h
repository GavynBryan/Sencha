#pragma once

#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
#include <render/ShadowCasterSet.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <span>

struct Registry;

class ShadowCasterExtractionSystem
{
public:
    void Extract(std::span<Registry*> registries,
                 const StaticMeshCache& meshes,
                 const MaterialCache& materials,
                 const MaterialSetCache& materialSets,
                 ShadowCasterSet& casters) const;
};
