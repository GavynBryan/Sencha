#include "RmlTextInputBridge.h"

#include <RmlUi/Core/TextInputContext.h>
#include <RmlUi/Core/Types.h>

#include <SDL3/SDL_keyboard.h>

RmlTextInputBridge::RmlTextInputBridge(SDL_Window* window)
    : Window(window)
{
}

RmlTextInputBridge::~RmlTextInputBridge()
{
    Stop();
}

void RmlTextInputBridge::OnActivate(Rml::TextInputContext* context)
{
    if (context == nullptr || Window == nullptr)
        return;

    Active = context;
    SDL_StartTextInput(Window);

    // Where the candidate window should sit. An IME that does not know where
    // the caret is puts its candidates over the text being composed, which is
    // the one place they must not be.
    Rml::Rectanglef caret;
    if (context->GetBoundingBox(caret))
    {
        const SDL_Rect area{
            static_cast<int>(caret.Left()),
            static_cast<int>(caret.Top()),
            static_cast<int>(caret.Width()),
            static_cast<int>(caret.Height()),
        };
        SDL_SetTextInputArea(Window, &area, 0);
    }
}

void RmlTextInputBridge::OnDeactivate(Rml::TextInputContext* context)
{
    // Only the field that has it may give it up. A deactivation arriving for a
    // field that already lost focus would otherwise stop input for the one that
    // just took it.
    if (context == Active)
        Stop();
}

void RmlTextInputBridge::OnDestroy(Rml::TextInputContext* context)
{
    // A field destroyed while focused -- a screen closing mid-edit, a document
    // reloading under the caret. Without this the platform keeps text input on
    // for a field that no longer exists.
    if (context == Active)
        Stop();
}

void RmlTextInputBridge::Stop()
{
    if (Active == nullptr)
        return;
    Active = nullptr;
    if (Window != nullptr)
        SDL_StopTextInput(Window);
}
