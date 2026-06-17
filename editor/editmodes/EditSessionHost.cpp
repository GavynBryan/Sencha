#include "EditSessionHost.h"

void EditSessionHost::SetSession(std::unique_ptr<IEditSession> session)
{
    Active = std::move(session);
}

void EditSessionHost::EndSession()
{
    Active.reset();
}

bool EditSessionHost::HasSession() const
{
    return Active != nullptr;
}

InputConsumed EditSessionHost::OnPointerDown(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    if (!Active)
        return InputConsumed::No;

    return Active->OnPointerDown(ctx, viewport, pointer);
}
