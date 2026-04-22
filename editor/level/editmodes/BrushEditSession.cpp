#include "BrushEditSession.h"

#include "BrushBodyHandle.h"
#include "BrushFaceHandle.h"
#include "../../level/BrushGeometry.h"
#include "../../interaction/IInteraction.h"
#include "../../interaction/InteractionHost.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/EditorViewport.h"

#include <limits>

BrushEditSession::BrushEditSession(SelectableRef selection, LevelScene& scene, LevelDocument& document)
    : Selection(selection)
    , Scene(scene)
    , Document(document)
{
    BuildHandles();
}

void BrushEditSession::BuildHandles()
{
    FaceHandles.clear();
    for (const BrushFaceDescriptor& face : BrushGeometry::EnumerateFaces(Scene, Selection.Entity))
    {
        if (face.Ref.IsValid())
            FaceHandles.push_back(std::make_unique<BrushFaceHandle>(face.Ref, Scene, Document));
    }

    BodyHandle = std::make_unique<BrushBodyHandle>(Selection.Entity, Scene, Document);
}

InputConsumed BrushEditSession::OnPointerDown(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    float bestDist = std::numeric_limits<float>::max();
    IHandle* bestHandle = nullptr;

    for (auto& handle : FaceHandles)
    {
        const HandleHit hit = handle->HitTest(viewport, pos);
        if (hit.Hit && hit.Distance < bestDist)
        {
            bestDist = hit.Distance;
            bestHandle = handle.get();
        }
    }

    if (bestHandle == nullptr)
    {
        const HandleHit bodyHit = BodyHandle->HitTest(viewport, pos);
        if (bodyHit.Hit)
            bestHandle = BodyHandle.get();
    }

    if (bestHandle == nullptr)
        return InputConsumed::No;

    auto interaction = bestHandle->BeginDrag(ctx, viewport, pos);
    if (interaction == nullptr)
        return InputConsumed::No;

    ctx.Interactions.Begin(std::move(interaction));
    return InputConsumed::Yes;
}
