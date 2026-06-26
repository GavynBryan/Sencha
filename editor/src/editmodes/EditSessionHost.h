#pragma once

#include "../input/InputEvent.h"

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

private:
    std::unique_ptr<ManipulatorSession> Active;
};
