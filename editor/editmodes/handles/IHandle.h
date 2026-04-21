#pragma once

#include "../../interaction/IInteraction.h"

#include <imgui.h>
#include <memory>

struct ToolContext;
struct EditorViewport;

struct HandleHit
{
    bool Hit = false;
    float Distance = 0.0f;
};

struct IHandle
{
    virtual HandleHit HitTest(const EditorViewport& viewport, ImVec2 screenPos) const = 0;
    virtual std::unique_ptr<IInteraction> BeginDrag(ToolContext& ctx,
                                                    const EditorViewport& viewport,
                                                    ImVec2 screenPos) const = 0;
    virtual ~IHandle() = default;
};
