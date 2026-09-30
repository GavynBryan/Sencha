#include "ShojiStatusBar.h"

#include "project/SourceReloadRoots.h"
#include "ui/chrome/ChromeBars.h"

#include <imgui.h>
#include <imgui_internal.h> // BeginViewportSideBar (reserves work-area space)

#include <cstdio>

ShojiStatusBar::ShojiStatusBar(UiPreviewSession& session, DocumentLibrary& library, SourceReloadRoots& watch)
    : Session(session)
    , Library(library)
    , Watch(watch)
{
}

void ShojiStatusBar::Draw()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float barHeight = ImGui::GetFrameHeight();
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;

    if (!ImGui::BeginViewportSideBar("##ShojiStatusBar", viewport, ImGuiDir_Down, barHeight, flags))
    {
        ImGui::End();
        return;
    }
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    EditorChrome::BarBackdrop(ImGui::GetWindowDrawList(), pos, { pos.x + size.x, pos.y + size.y },
                              EditorChrome::BarEdge::Top);
    if (ImGui::BeginMenuBar())
    {
        EditorChrome::Readout("DOC", Session.IsOpen() ? Session.PackagePath().c_str() : "-",
                              Session.IsOpen() ? EditorChrome::LedState::On : EditorChrome::LedState::Off);
        EditorChrome::Divider();
        char surface[48];
        std::snprintf(surface, sizeof(surface), "%u x %u @%.2g", Session.Resolution().Width,
                      Session.Resolution().Height, Session.DisplayScale());
        EditorChrome::Readout("SURFACE", surface);
        EditorChrome::Divider();
        EditorChrome::Readout("MODE", Session.Mode() == UiPreviewSession::PointerMode::Inspect ? "Inspect"
                                      : Session.IsActivated()                                  ? "Interact (keys)"
                                                                                               : "Interact");
        EditorChrome::Divider();
        char roots[48];
        std::snprintf(roots, sizeof(roots), "%zu roots, %zu files", Watch.RootCount(), Watch.WatchedFileCount());
        EditorChrome::Readout("WATCH", roots,
                              Watch.RootCount() > 0 ? EditorChrome::LedState::On : EditorChrome::LedState::Off);
        EditorChrome::Divider();
        // What the panel shows by default, so a lit readout and an empty table
        // cannot disagree: info is noise a rebuild produces every time.
        std::size_t errors = 0;
        std::size_t warnings = 0;
        for (const UiDiagnostic& d : Session.Diagnostics())
        {
            errors += d.Severity == UiDiagnosticSeverity::Error;
            warnings += d.Severity == UiDiagnosticSeverity::Warning;
        }
        char diagnostics[32];
        std::snprintf(diagnostics, sizeof(diagnostics), "%zu", errors + warnings);
        EditorChrome::Readout("DIAG", diagnostics,
                              errors > 0      ? EditorChrome::LedState::Alert
                              : warnings > 0  ? EditorChrome::LedState::On
                                              : EditorChrome::LedState::Off);
        ImGui::EndMenuBar();
    }
    ImGui::End();
}
