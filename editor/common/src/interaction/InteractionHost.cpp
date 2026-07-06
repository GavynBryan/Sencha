#include "InteractionHost.h"

#include "IInteraction.h"

void InteractionHost::Begin(ToolContext& ctx, std::unique_ptr<IInteraction> interaction)
{
    Cancel(ctx); // revert any in-flight interaction before replacing it
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
                                              const PointerEvent& pointer)
{
    if (!Active)
        return InputConsumed::No;

    Active->OnPointerMove(ctx, viewport, pointer);
    return InputConsumed::Yes;
}

InputConsumed InteractionHost::OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    if (!Active)
        return InputConsumed::No;

    auto interaction = std::move(Active);
    try
    {
        interaction->OnPointerUp(ctx, viewport, pointer); // commit
    }
    catch (...)
    {
        // Commit threw: revert the live preview so an interrupted commit can't
        // strand uncommitted scene state, then propagate.
        interaction->OnCancel(ctx);
        throw;
    }
    return InputConsumed::Yes;
}
