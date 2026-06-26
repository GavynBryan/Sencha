#include "ManipulatorTargets.h"

#include "../meshedit/ManipulationSink.h"
#include "../meshedit/MeshEditService.h"
#include "../meshedit/MeshElementKindTraits.h"

namespace
{
// Picks the entity to edit (primary's if it matches the wanted kind, else the
// first matching ref) and collects that entity's refs of that kind.
EntityId GatherModeElements(const SelectionSnapshot& selection, SelectableKind wantKind,
                            std::vector<SelectableRef>& outElements)
{
    EntityId entity = {};
    if (selection.Primary.IsValid() && selection.Primary.Kind == wantKind)
        entity = selection.Primary.Entity;
    else
    {
        for (SelectableRef ref : selection.Items)
            if (ref.IsValid() && ref.Kind == wantKind) { entity = ref.Entity; break; }
    }
    if (!entity.IsValid())
        return {};
    for (SelectableRef ref : selection.Items)
        if (ref.IsValid() && ref.Kind == wantKind && ref.Entity == entity)
            outElements.push_back(ref);
    return entity;
}
}

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

std::optional<ElementTarget> ResolveElementTarget(const ManipulatorContext& ctx, MeshElementKind kind)
{
    std::vector<SelectableRef> elements;
    const EntityId entity = GatherModeElements(ctx.Selection, Traits(kind).Selectable, elements);
    if (!entity.IsValid() || elements.empty())
        return std::nullopt;
    const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(entity);
    if (!resolved.has_value() || resolved->Mesh == nullptr)
        return std::nullopt;
    return ElementTarget{ entity, *resolved->Mesh, resolved->Transform, std::move(elements) };
}
