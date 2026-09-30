#pragma once

#include "input/InputEvent.h"

#include <memory>

struct ToolContext;
struct EditorViewport;
class ManipulatorSession;

class EditSessionHost
{
public:
    ~EditSessionHost();

    void SetSession(std::unique_ptr<ManipulatorSession> session);
    void EndSession();
    bool HasSession() const;

    InputConsumed OnPointerDown(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer);

    // Gizmo hover tracking, forwarded to the active session (see
    // ManipulatorSession::UpdateHover for why hover comes from the input path).
    void UpdateHover(const EditorViewport& viewport, ImVec2 pos);
    void ClearHover();

private:
    std::unique_ptr<ManipulatorSession> Active;
};
