#include "MeshEditSession.h"

#include "SelectionPivot.h"
#include "../../editmodes/TranslateGizmo.h"
#include "../BrushEditTarget.h"
#include "../LevelDocument.h"
#include "../LevelScene.h"
#include "../commands/MoveEntityCommand.h"
#include "../../commands/CommandStack.h"
#include "../../interaction/InteractionHost.h"
#include "../../meshedit/MeshEditService.h"
#include "../../selection/SelectionService.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/EditorViewport.h"

#include <memory>
#include <utility>
#include <vector>

namespace
{
// Moves an entity by its transform (never its mesh), committing as a MoveEntity.
class ObjectTranslateHandler : public IGizmoHandler
{
public:
    ObjectTranslateHandler(EntityId entity, Transform3f initial,
                           LevelScene& scene, LevelDocument& document, CommandStack& commands)
        : Entity(entity), Initial(initial), Scene(scene), Document(document), Commands(commands)
    {
    }

    void Preview(const GizmoDelta& delta) override { Scene.SetTransform(Entity, WithDelta(delta)); }

    void Commit(const GizmoDelta& delta) override
    {
        Commands.Execute(std::make_unique<MoveEntityCommand>(
            Entity, Initial, WithDelta(delta), Scene, Document));
    }

    void Cancel() override { Scene.SetTransform(Entity, Initial); }

private:
    [[nodiscard]] Transform3f WithDelta(const GizmoDelta& delta) const
    {
        Transform3f result = Initial;
        result.Position += delta.Translation;
        return result;
    }

    EntityId Entity;
    Transform3f Initial;
    LevelScene& Scene;
    LevelDocument& Document;
    CommandStack& Commands;
};

// Moves selected mesh vertices through MeshEditService, committing the validated
// result as an EditBrushMeshCommand via the brush edit target.
class ElementTranslateHandler : public IGizmoHandler
{
public:
    ElementTranslateHandler(EntityId entity, BrushMesh initial, Transform3f transform,
                            std::vector<SelectableRef> elements, MeshElementKind kind,
                            LevelScene& scene, LevelDocument& document, CommandStack& commands,
                            MeshEditService& service)
        : Entity(entity)
        , Initial(std::move(initial))
        , Transform(transform)
        , Elements(std::move(elements))
        , Kind(kind)
        , Scene(scene)
        , Document(document)
        , Commands(commands)
        , Service(service)
    {
    }

    void Preview(const GizmoDelta& delta) override
    {
        if (auto mesh = Service.TranslateElements(Initial, Transform, Elements, Kind, delta.Translation, false))
            Scene.SetBrushMesh(Entity, *mesh);
    }

    void Commit(const GizmoDelta& delta) override
    {
        std::optional<BrushMesh> mesh =
            Service.TranslateElements(Initial, Transform, Elements, Kind, delta.Translation, true);
        if (!mesh.has_value())
        {
            // Unusable geometry (e.g. a degenerate result): revert to the original.
            Scene.SetBrushMesh(Entity, Initial);
            return;
        }

        BrushEditTarget target(Scene, Document);
        Commands.Execute(target.MakeEditCommand(Entity, Initial, std::move(*mesh)));
    }

    void Cancel() override { Scene.SetBrushMesh(Entity, Initial); }

private:
    EntityId Entity;
    BrushMesh Initial;
    Transform3f Transform;
    std::vector<SelectableRef> Elements;
    MeshElementKind Kind;
    LevelScene& Scene;
    LevelDocument& Document;
    CommandStack& Commands;
    MeshEditService& Service;
};

SelectableKind ElementSelectableKind(MeshElementKind kind)
{
    switch (kind)
    {
    case MeshElementKind::Vertex: return SelectableKind::Vertex;
    case MeshElementKind::Edge:   return SelectableKind::Edge;
    case MeshElementKind::Face:   return SelectableKind::Face;
    case MeshElementKind::Object:
    default:                      return SelectableKind::Entity;
    }
}

// Chooses the entity to edit (the primary's entity when it matches the mode, else
// the first matching ref's entity) and collects that entity's refs of the mode.
EntityId GatherModeElements(const SelectionSnapshot& selection,
                            SelectableKind wantKind,
                            std::vector<SelectableRef>& outElements)
{
    EntityId entity = {};
    if (selection.Primary.IsValid() && selection.Primary.Kind == wantKind)
        entity = selection.Primary.Entity;
    else
    {
        for (SelectableRef ref : selection.Items)
        {
            if (ref.IsValid() && ref.Kind == wantKind)
            {
                entity = ref.Entity;
                break;
            }
        }
    }

    if (!entity.IsValid())
        return {};

    for (SelectableRef ref : selection.Items)
    {
        if (ref.IsValid() && ref.Kind == wantKind && ref.Entity == entity)
            outElements.push_back(ref);
    }
    return entity;
}

std::unique_ptr<IGizmoHandler> MakeObjectHandler(ToolContext& ctx,
                                                 const SelectionSnapshot& selection)
{
    EntityId entity = {};
    if (selection.Primary.IsEntity())
        entity = selection.Primary.Entity;
    else
    {
        for (SelectableRef ref : selection.Items)
        {
            if (ref.IsEntity())
            {
                entity = ref.Entity;
                break;
            }
        }
    }

    const Transform3f* transform = entity.IsValid() ? ctx.Scene.TryGetTransform(entity) : nullptr;
    if (transform == nullptr)
        return nullptr;

    return std::make_unique<ObjectTranslateHandler>(
        entity, *transform, ctx.Scene, ctx.Document, ctx.Commands);
}

std::unique_ptr<IGizmoHandler> MakeElementHandler(ToolContext& ctx,
                                                  const SelectionSnapshot& selection,
                                                  MeshElementKind kind)
{
    std::vector<SelectableRef> elements;
    const EntityId entity = GatherModeElements(selection, ElementSelectableKind(kind), elements);
    if (!entity.IsValid() || elements.empty())
        return nullptr;

    const BrushMesh* mesh = ctx.Scene.TryGetBrushMesh(entity);
    const Transform3f* transform = ctx.Scene.TryGetTransform(entity);
    if (mesh == nullptr || transform == nullptr)
        return nullptr;

    return std::make_unique<ElementTranslateHandler>(
        entity, *mesh, *transform, std::move(elements), kind,
        ctx.Scene, ctx.Document, ctx.Commands, ctx.MeshEdit);
}
}

MeshEditSession::MeshEditSession()
    : Gizmo(std::make_unique<TranslateGizmo>())
{
}

InputConsumed MeshEditSession::OnPointerDown(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    const MeshElementKind kind = ctx.MeshEdit.GetElementKind();
    const SelectionSnapshot selection = ctx.Selection.GetSnapshot();

    const std::optional<Vec3d> pivot = ComputeSelectionPivot(ctx.Scene, selection, kind);
    if (!pivot.has_value())
        return InputConsumed::No;

    Gizmo->SetPivot(*pivot);
    const int part = Gizmo->HitTest(viewport, pos);
    if (part == 0)
        return InputConsumed::No;

    std::unique_ptr<IGizmoHandler> handler = (kind == MeshElementKind::Object)
        ? MakeObjectHandler(ctx, selection)
        : MakeElementHandler(ctx, selection, kind);
    if (handler == nullptr)
        return InputConsumed::No;

    auto interaction = Gizmo->BeginDrag(part, viewport, pos, std::move(handler));
    if (interaction == nullptr)
        return InputConsumed::No;

    ctx.Interactions.Begin(std::move(interaction));
    return InputConsumed::Yes;
}
