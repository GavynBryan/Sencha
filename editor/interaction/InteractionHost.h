#pragma once

#include "IInteraction.h"

#include "../input/InputEvent.h"

#include <imgui.h>
#include <memory>

struct ToolContext;
struct EditorViewport;

class InteractionHost
{
public:
    void Begin(std::unique_ptr<IInteraction> interaction);
    void Cancel(ToolContext& ctx);
    bool IsActive() const;

    InputConsumed OnPointerMove(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos, ImVec2 delta);
    InputConsumed OnPointerUp(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos);

private:
    std::unique_ptr<IInteraction> Active;
};
