#pragma once

#include "ITool.h"

#include "input/InputEvent.h"

#include <imgui.h>

#include <memory>
#include <string_view>
#include <vector>

struct EditorViewport;
struct ToolContext;

class ToolRegistry
{
public:
    explicit ToolRegistry(ToolContext& context);

    void Register(std::unique_ptr<ITool> tool);
    bool Activate(std::string_view id);
    bool Activate(std::size_t index);

    [[nodiscard]] ITool* GetActiveTool();
    [[nodiscard]] const ITool* GetActiveTool() const;
    [[nodiscard]] int GetActiveIndex() const;
    [[nodiscard]] const std::vector<std::unique_ptr<ITool>>& GetTools() const;

    // The shared context, for UI that drives tool operations directly (e.g. the
    // Tool Properties panel regenerating a tool's pending preview).
    [[nodiscard]] ToolContext& GetContext() { return *Context; }

    // Points the registry at a new context. The tools outlive any one document,
    // so a document swap rebinds them here instead of rebuilding them, which is
    // what lets a tool hold settings and stay active across the swap. The caller
    // resolves in-flight tool state before rebinding; nothing here does.
    void Rebind(ToolContext& context) { Context = &context; }

    // Aborts the active tool's in-progress gesture, if any. (W4.)
    void Cancel();

    InputConsumed Click(EditorViewport& viewport, const PointerEvent& pointer);
    InputConsumed DoubleClick(EditorViewport& viewport, const PointerEvent& pointer);
    std::unique_ptr<IInteraction> BeginDrag(EditorViewport& viewport, const PointerEvent& pressPointer);
    InputConsumed Hover(EditorViewport& viewport, ImVec2 pos);
    void HoverEnd();
    InputConsumed OnInput(const InputEvent& event);

private:
    ToolContext* Context;
    std::vector<std::unique_ptr<ITool>> Tools;
    // -1 until something activates. Registration alone never activates: a tool
    // that was never entered through Activate has not had OnActivate run, and
    // treating it as current would skip that setup.
    int ActiveIndex = -1;
};
