#pragma once

#include <RmlUi/Core/Header.h>
#include <RmlUi/Core/Traits.h>
#include <RmlUi/Core/TextInputHandler.h>

struct SDL_Window;

//=============================================================================
// RmlTextInputBridge
//
// Turns a focused text field into platform text input, and back again.
//
// This is what makes typing work as typing: the platform reports composed
// characters -- a dead key resolved, an IME candidate committed, an AltGr
// combination -- and a document receives those, rather than a keycode stream
// somebody tried to reassemble into text. Reconstructing characters from keys
// is wrong in every locale that is not the author's.
//
// It also decides when the on-screen keyboard and the IME candidate window
// appear at all, which is why text input is started and stopped rather than
// left on: a platform with text input always active is a platform where the
// IME is always eligible to eat a keystroke meant for the game.
//=============================================================================
class RmlTextInputBridge final : public Rml::TextInputHandler
{
public:
    // The window text input is requested against. Null disables the bridge --
    // a headless process has no input method to talk to.
    explicit RmlTextInputBridge(SDL_Window* window);
    ~RmlTextInputBridge() override;

    void OnActivate(Rml::TextInputContext* context) override;
    void OnDeactivate(Rml::TextInputContext* context) override;
    void OnDestroy(Rml::TextInputContext* context) override;

    // Whether a text field currently has focus. A raw reader of the device
    // snapshot gates on this: the keystrokes being typed into a field are in
    // that snapshot, faithfully, and were not meant for whatever else reads it.
    [[nodiscard]] bool IsTextInputActive() const { return Active != nullptr; }

private:
    void Stop();

    SDL_Window* Window = nullptr;
    Rml::TextInputContext* Active = nullptr;
};
