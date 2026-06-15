#include "BrushEditSession.h"

#include "BrushBodyHandle.h"
#include "../../interaction/IInteraction.h"
#include "../../interaction/InteractionHost.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/EditorViewport.h"

BrushEditSession::BrushEditSession(SelectableRef selection, LevelScene& scene, LevelDocument& document)
    : Selection(selection)
    , Scene(scene)
    , Document(document)
{
    BuildHandles();
}

void BrushEditSession::BuildHandles()
{
    // Whole-brush move only. Face editing is now done by selecting a face (mesh
    // picking) and using the inspector Brush Tools; the box-axis face drag-handles
    // were retired with the mesh-brush model. Per-face drag handles return in a
    // later pass over the mesh-face descriptors. (03-brush-representation.md §4)
    BodyHandle = std::make_unique<BrushBodyHandle>(Selection.Entity, Scene, Document);
}

InputConsumed BrushEditSession::OnPointerDown(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    const HandleHit bodyHit = BodyHandle->HitTest(viewport, pos);
    if (!bodyHit.Hit)
        return InputConsumed::No;

    auto interaction = BodyHandle->BeginDrag(ctx, viewport, pos);
    if (interaction == nullptr)
        return InputConsumed::No;

    ctx.Interactions.Begin(std::move(interaction));
    return InputConsumed::Yes;
}
