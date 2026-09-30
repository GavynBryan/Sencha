#pragma once

#include "authoring/UiPreviewModel.h"

#include <math/geometry/2d/Rect2d.h>
#include <ui/UiAction.h>
#include <ui/UiDiagnostic.h>
#include <ui/UiService.h>

#include <cstddef>
#include <deque>
#include <optional>
#include <string>

//=============================================================================
// UiPreviewSession
//
// One document being looked at: the offscreen surface it is laid out on, the
// screen opened with its preview model, the resolution and display scale the
// author chose, how the pointer is being used, and everything the document
// said or the layer reported while it was open.
//
// GUI-free. It knows a UiService and a model; it knows nothing of the window,
// the panels or the render target that shows it. That is what lets a headless
// test drive the whole loop, and what lets Kyusu construct one later and hang
// its own panels on it.
//
// It is the ONE drainer of the service's diagnostics. DrainDiagnostics is
// destructive; a panel and a collator each draining it would starve each
// other. The session drains once per Poll into a bounded history that
// everything else reads as a view.
//=============================================================================
class UiPreviewSession
{
public:
    enum class PointerMode : std::uint8_t
    {
        // Pointer and keys reach the document; keys only once it is activated.
        Interact,
        // Nothing reaches the document. The host maps the hover point itself
        // and asks ElementAt, so a measured button does not light up.
        Inspect,
    };

    explicit UiPreviewSession(UiService& ui);
    ~UiPreviewSession();
    UiPreviewSession(const UiPreviewSession&) = delete;
    UiPreviewSession& operator=(const UiPreviewSession&) = delete;

    // Opens `packagePath` against `model` on this session's surface, closing
    // whatever was open. False when the layer refused the screen; the refusal
    // is in Diagnostics().
    bool Open(std::string packagePath, UiPreviewModel model);
    // Closes and reopens the same package against the current model -- what a
    // model edit needs, since a declaration is made before the document loads.
    bool Reopen();
    void Close();
    [[nodiscard]] bool IsOpen() const { return Screen.IsValid(); }

    [[nodiscard]] UiSurfaceId Surface() const { return SurfaceId; }
    // The layer the session drives, for a panel that asks it about elements or
    // steers focus. Never for draining diagnostics: that is the session's.
    [[nodiscard]] UiService& Service() { return Ui; }
    [[nodiscard]] UiScreenHandle OpenScreen() const { return Screen; }
    [[nodiscard]] const std::string& PackagePath() const { return Package; }
    [[nodiscard]] UiPreviewModel& Model() { return Current; }
    [[nodiscard]] const UiPreviewModel& Model() const { return Current; }

    // Live: the document re-lays out.
    void SetResolution(RenderExtent size);
    [[nodiscard]] RenderExtent Resolution() const { return Size; }
    void SetDisplayScale(float scale);
    [[nodiscard]] float DisplayScale() const { return Scale; }

    // Where the surface is shown in the window, for pointer mapping. The host
    // sets it every frame it draws the preview.
    void SetPlacement(std::optional<Rect2d> windowRect);

    void SetPointerMode(PointerMode mode);
    [[nodiscard]] PointerMode Mode() const { return Pointer; }
    // Interact only: whether the document has the keyboard.
    void SetActivated(bool activated);
    [[nodiscard]] bool IsActivated() const { return Activated; }

    // A stylesheet to inject as the host's theme (the editor's, for the
    // editor's own documents), or nullopt for the document as a game shows it.
    void SetHostTheme(std::optional<std::string> themeRcss);
    [[nodiscard]] bool HasHostTheme() const { return Theme.has_value(); }

    // Once per frame: drains the layer's diagnostics and this screen's actions
    // into the histories below.
    void Poll();
    [[nodiscard]] const std::deque<UiDiagnostic>& Diagnostics() const { return DiagnosticHistory; }
    [[nodiscard]] const std::deque<UiAction>& Actions() const { return ActionHistory; }
    void ClearDiagnostics() { DiagnosticHistory.clear(); }
    void ClearActions() { ActionHistory.clear(); }

    static constexpr std::size_t kDiagnosticHistory = 1024;
    static constexpr std::size_t kActionHistory = 256;

private:
    void ApplyInputPolicy();

    UiService& Ui;
    UiSurfaceId SurfaceId;
    UiScreenHandle Screen;
    std::string Package;
    UiPreviewModel Current;
    RenderExtent Size{ 1920, 1080 };
    float Scale = 1.0f;
    PointerMode Pointer = PointerMode::Interact;
    bool Activated = false;
    std::optional<std::string> Theme;
    std::deque<UiDiagnostic> DiagnosticHistory;
    std::deque<UiAction> ActionHistory;
};
