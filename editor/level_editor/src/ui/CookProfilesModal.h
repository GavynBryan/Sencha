#pragma once

#include "presenters/PropertySheet.h"
#include "project/CookProfileEditor.h"

#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

class UiService;
struct ProjectDescriptor;

//=============================================================================
// CookProfilesModal
//
// The project's cook profiles as a dialog: the list, the selected profile's
// settings as rows, and duplicate, delete, validate and save. It presents a
// CookProfileEditor and passes on what the document asks for; nothing here
// decides what a profile may hold.
//=============================================================================
class CookProfilesModal
{
public:
    CookProfilesModal(UiService& ui, ProjectDescriptor& project);
    ~CookProfilesModal();

    CookProfilesModal(const CookProfilesModal&) = delete;
    CookProfilesModal& operator=(const CookProfilesModal&) = delete;

    // Where it opens from now on: the window's surface for documents a person
    // works in. An open dialog closes when its window changes.
    void MoveTo(UiSurfaceId surface);

    // Opens the dialog; opening it again changes nothing.
    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const { return Screen.IsValid(); }
    [[nodiscard]] UiScreenHandle CurrentScreen() const { return Screen; }
    [[nodiscard]] CookProfileEditor& Editor() { return Profiles; }

    // Per frame, before the UI updates: act on what the document asked for,
    // then publish what it should show.
    void Update();

private:
    void HandleActions();
    void Publish();

    UiService& Ui;
    CookProfileEditor Profiles;
    PropertySheet Sheet;
    UiSurfaceId Surface;
    UiScreenHandle Screen;
};
