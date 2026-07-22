#include "EditorLightGather.h"

#include "document/EditorDocument.h"

#include <render/LightComponentTypes.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>

namespace
{
    constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;

    void HashBytes(std::uint64_t& hash, const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= bytes[index];
            hash *= kFnvPrime;
        }
    }
}

EditorLightGather GatherEditorLights(const EditorDocument& document,
                                     bool skipBakedDirect,
                                     float globalShadowSoftness)
{
    EditorLightGather result;
    result.ContentHash = kFnvOffset;
    const EditorScene& scene = document.GetScene();
    const Registry& registry = scene.GetRegistry();
    const World& world = registry.Components;
    for (const EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityVisible(entity))
            continue;
        const Transform3f* transform = scene.TryGetTransform(entity);
        if (transform == nullptr)
            continue;

        if (const PointLightComponent* point = world.TryGet<PointLightComponent>(entity);
            point != nullptr && point->Enabled
            && IsUsableForwardLight(point->Intensity, point->Range))
        {
            HashBytes(result.ContentHash, &transform->Position,
                      sizeof(transform->Position));
            HashBytes(result.ContentHash, point, sizeof(*point));
            if (!skipBakedDirect
                || point->BakeContribution != LightBakeContribution::Direct)
                result.Candidates.push_back(MakePointLightCandidate(
                    MakeRenderEntityKey(registry, entity), transform->Position,
                    *point, globalShadowSoftness));
        }

        if (const SpotLightComponent* spot = world.TryGet<SpotLightComponent>(entity);
            spot != nullptr && spot->Enabled
            && IsUsableForwardLight(spot->Intensity, spot->Range))
        {
            HashBytes(result.ContentHash, &transform->Position,
                      sizeof(transform->Position));
            HashBytes(result.ContentHash, spot, sizeof(*spot));
            if (!skipBakedDirect
                || spot->BakeContribution != LightBakeContribution::Direct)
                result.Candidates.push_back(MakeSpotLightCandidate(
                    MakeRenderEntityKey(registry, entity), *transform,
                    *spot, globalShadowSoftness));
        }
    }
    return result;
}
