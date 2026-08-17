#include "EditorLightGather.h"
#include "EditorRenderEntityKey.h"

#include "document/EditorDocument.h"

#include <core/hash/Fnv1a.h>
#include <render/LightComponentTypes.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>

EditorLightGather GatherEditorLights(const EditorDocument& document,
                                     bool skipBakedDirect,
                                     float globalShadowSoftness)
{
    EditorLightGather result;
    result.ContentHash = kFnv1aOffsetBasis;
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
            HashFnv1aValue(result.ContentHash, transform->Position);
            HashFnv1aValue(result.ContentHash, *point);
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
            HashFnv1aValue(result.ContentHash, transform->Position);
            HashFnv1aValue(result.ContentHash, *spot);
            if (!skipBakedDirect
                || spot->BakeContribution != LightBakeContribution::Direct)
                result.Candidates.push_back(MakeSpotLightCandidate(
                    MakeRenderEntityKey(registry, entity), *transform,
                    *spot, globalShadowSoftness));
        }
    }
    return result;
}
