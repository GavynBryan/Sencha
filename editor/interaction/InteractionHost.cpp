#include "InteractionHost.h"

#include "IInteraction.h"

void InteractionHost::Begin(std::unique_ptr<IInteraction> interaction)
{
    Active = std::move(interaction);
}

void InteractionHost::Cancel(ToolContext& ctx)
{
    if (Active)
    {
        Active->OnCancel(ctx);
        Active.reset();
    }
}

bool InteractionHost::IsActive() const
{
    return Active != nullptr;
}

InputConsumed InteractionHost::OnPointerMove(ToolContext& ctx,
                                              EditorViewport& viewport,
                                              ImVec2 pos,
                                              ImVec2 delta)
{
    if (!Active)
        return InputConsumed::No;

    Active->OnPointerMove(ctx, viewport, pos, delta);
    return InputConsumed::Yes;
}

InputConsumed InteractionHost::OnPointerUp(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    if (!Active)
        return InputConsumed::No;

    auto interaction = std::move(Active);
    interaction->OnPointerUp(ctx, viewport, pos);
    return InputConsumed::Yes;
}
