#pragma once

#include <ui/UiScreenDesc.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <string>
#include <vector>

class UiService;
struct ProjectDescriptor;

//=============================================================================
// CookProfilesModal
//
// Kyusu's first authored workflow: the cook-profile editor as an RML document
// over the editor, rather than an ImGui panel inside it.
//
// The division of labour is the point. The document presents a list and a form
// and raises named requests; this decides what a request means, validates it,
// changes the ProjectDescriptor, and saves through the same path the ImGui panel
// uses. Nothing in the document touches a profile, and nothing here draws.
//
// That is also what makes the edit safe to abandon. A name being typed lives in
// the screen's presentation copy until this reads it back on a save, so closing
// the dialog mid-edit discards it with nothing to roll back.
//
// The ImGui panel stays. This coexists with it until it is demonstrably better,
// which is the only honest way to find out.
//=============================================================================
class CookProfilesModal
{
public:
    // The surface is the host's, not this dialog's: modality is arbitrated
    // within a surface, so a dialog on its own would take focus from nothing.
    CookProfilesModal(UiService& ui, UiSurfaceId surface, ProjectDescriptor* project);

    // Opens the dialog, or brings the open one back to the front of the host's
    // attention. Safe to call when already open.
    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const { return Screen.IsValid(); }

    // The open screen, for a caller that wants to read what is currently being
    // presented -- a test asserting the form followed the selection, or a host
    // wiring a second surface against the same document.
    [[nodiscard]] UiScreenHandle CurrentScreen() const { return Screen; }

    // Per frame, before the engine updates the UI: act on what the document
    // asked for, then publish what it should now show.
    void Update();

private:
    // Property and action ids, positional and declared together with the
    // description below so the two cannot drift.
    enum class Property : std::size_t { Profiles = 0, Steps, SelectedIndex,
                                        SelectedName, Status, Dirty, BuiltIn };
    enum class Action : std::size_t { Select = 0, Save, Validate, Close };

    [[nodiscard]] static UiScreenDesc Describe();
    void Publish();
    void HandleSelect(std::size_t index);
    // False when the edit was refused, with Status saying why. A caller that
    // carries on regardless would overwrite that explanation with its own and
    // report success for something it did not do.
    [[nodiscard]] bool ApplyNameEdit();
    void Save();
    void Validate();

    // The profiles as the dialog lists them: the project's own, plus the
    // built-ins it inherits. Rebuilt whenever the project changes under us.
    [[nodiscard]] std::vector<struct CookProfile> ResolvedProfiles() const;

    UiService& Ui;
    ProjectDescriptor* Project = nullptr;

    UiSurfaceId Surface;
    UiScreenHandle Screen;

    std::size_t Selected = 0;
    // What the name field was last filled from. The field is refilled when the
    // selection moves and never otherwise, because republishing it every frame
    // would delete what somebody is typing between keystrokes.
    std::size_t NameShowing = static_cast<std::size_t>(-1);
    std::string Status;
    bool Dirty = false;
};
