#include "ManipulatorTargets.h"

#include "meshedit/ManipulationSink.h"
#include "meshedit/MeshEditService.h"
#include "meshedit/MeshElementKindTraits.h"
#include "meshedit/SelectionConversion.h"

std::vector<ObjectTarget> GatherObjectTargets(const ManipulatorContext& ctx)
{
    std::vector<ObjectTarget> items;
    for (SelectableRef ref : ctx.Selection.Items)
    {
        if (!ref.IsEntity())
            continue;
        if (const std::optional<Transform3f> transform = ctx.Sink.ResolveTransform(ref.Entity))
            items.push_back({ ref.Entity, *transform });
    }
    return items;
}

std::vector<ElementTarget> ResolveElementTargets(const ManipulatorContext& ctx, MeshElementKind kind)
{
    std::vector<ElementTarget> targets;
    for (EntityRefGroup& group : GroupSelectionByEntity(ctx.Selection, Traits(kind).Selectable))
    {
        const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(group.Entity);
        if (!resolved.has_value() || resolved->Mesh == nullptr)
            continue;
        targets.push_back(ElementTarget{
            group.Entity, *resolved->Mesh, resolved->Transform, std::move(group.Refs) });
    }
    return targets;
}
