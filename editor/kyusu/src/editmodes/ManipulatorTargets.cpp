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

        // Topmost selected ancestor only: a selected child of a selected
        // ancestor follows that ancestor through transform propagation, so
        // handing it the delta as well would move it twice.
        bool coveredByAncestor = false;
        for (EntityId ancestor = ctx.Sink.GetParent(ref.Entity);
             ancestor.IsValid() && !coveredByAncestor;
             ancestor = ctx.Sink.GetParent(ancestor))
        {
            for (const SelectableRef& other : ctx.Selection.Items)
                if (other.IsEntity() && other.Entity == ancestor)
                {
                    coveredByAncestor = true;
                    break;
                }
        }
        if (coveredByAncestor)
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
