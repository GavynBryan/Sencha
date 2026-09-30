#pragma once

#include "actions/ActionContext.h"

#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

class PromptQueue;
class UiService;

//=============================================================================
// PromptDialog
//
// The prompt queue's front question, over the whole window until it is
// answered: its choices as keys, Escape as its "cancel". Every window shows the
// same question, so it is answered wherever the person is looking.
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
    [[nodiscard]] bool IsShowing() const { return Screen.IsValid(); }

private:
    void Show();
    void Hide();
    void Publish();

    UiService& Ui;
    UiSurfaceId Surface;
    PromptQueue& Prompts;
    ActionContextStack& Actions;

    UiScreenHandle Screen;
    // Escape belongs to the question while one is showing.
    ActionContext EscapeActions;
    ActionContextStack::Layer EscapeLayer;
};
