#pragma once

#include "IEditSession.h"

#include <memory>

struct ToolContext;
struct EditorViewport;

class EditSessionHost
{
public:
    void SetSession(std::unique_ptr<IEditSession> session);
    void EndSession();
    bool HasSession() const;

    InputConsumed OnPointerDown(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos);

private:
    std::unique_ptr<IEditSession> Active;
};
