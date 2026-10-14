#pragma once

#include "ui/UiScreenSlot.h"

#include "actions/ActionContext.h"

#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstdint>
#include <string>

class PromptQueue;
class UiService;

//=============================================================================
// PromptDialog
// The prompt queue's front question over the whole window, in every window.
// Escape answers its cancel; Return answers its first choice.
//=============================================================================
class PromptDialog
{
public:
    PromptDialog(UiService& ui, UiSurfaceId surface, PromptQueue& prompts, ActionContextStack& actions);
    ~PromptDialog();

    PromptDialog(const PromptDialog&) = delete;
    PromptDialog& operator=(const PromptDialog&) = delete;

    // Per frame: act on an answer, then show whatever is in front now.
    void Update();
    [[nodiscard]] bool IsShowing() const { return Screen.IsOpen(); }

private:
    void Show();
    void Hide();
    void Publish();

    UiService& Ui;
    PromptQueue& Prompts;
    ActionContextStack& Actions;

    UiScreenSlot Screen;
    // The question shown, and what has been written in its field.
    std::uint64_t Showing = 0;
    std::string Written;
    // Escape belongs to the question while one is showing.
    ActionContext EscapeActions;
    ActionContextStack::Layer EscapeLayer;
};
