#include "DiagnosticsPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "ui/chrome/ChromeControls.h"

#include <imgui.h>

#include <cstdio>
#include <string>

namespace
{
    const char* KindLabel(UiDiagnosticKind kind)
    {
        switch (kind)
        {
        case UiDiagnosticKind::UnsupportedStyle: return "unsupported style";
        case UiDiagnosticKind::PackageUnavailable: return "package unavailable";
        case UiDiagnosticKind::ResourceUnresolved: return "resource unresolved";
        case UiDiagnosticKind::ModelRefused: return "model refused";
        case UiDiagnosticKind::DocumentInvalid: return "document invalid";
        case UiDiagnosticKind::RebuildFailed: return "rebuild failed";
        case UiDiagnosticKind::BindingMissing: return "binding missing";
        case UiDiagnosticKind::MemberMissing: return "member missing";
        case UiDiagnosticKind::EventCallbackMissing: return "action missing";
        case UiDiagnosticKind::DocumentEngineOther: return "document engine";
        }
        return "?";
    }

    const char* SourceLabel(UiDiagnosticSource source)
    {
        switch (source)
        {
        case UiDiagnosticSource::Cook: return "cook";
        case UiDiagnosticSource::Runtime: return "runtime";
        case UiDiagnosticSource::DocumentEngine: return "engine";
        }
        return "?";
    }

    ImVec4 SeverityColor(UiDiagnosticSeverity severity)
    {
        switch (severity)
        {
        case UiDiagnosticSeverity::Error: return EditorUi::Danger;
        case UiDiagnosticSeverity::Warning: return EditorUi::Warning;
        case UiDiagnosticSeverity::Info: return EditorUi::TextDim;
        }
        return EditorUi::TextDim;
    }

    bool IsBindingKind(UiDiagnosticKind kind)
    {
        return kind == UiDiagnosticKind::BindingMissing || kind == UiDiagnosticKind::MemberMissing
               || kind == UiDiagnosticKind::EventCallbackMissing;
    }

    // Only what the engine knew, in the order a reader looks for it.
    std::string Where(const UiDiagnostic& d)
    {
        std::string where;
        if (d.Path)
        {
            where = *d.Path;
            if (d.Line)
                where += ":" + std::to_string(*d.Line);
        }
        char buffer[48];
        if (d.Screen.IsValid())
        {
            std::snprintf(buffer, sizeof(buffer), "%sscreen %u", where.empty() ? "" : "  ", d.Screen.Index);
            where += buffer;
        }
        if (d.Surface.IsValid())
        {
            std::snprintf(buffer, sizeof(buffer), "%ssurface %u", where.empty() ? "" : "  ", d.Surface.Index);
            where += buffer;
        }
        return where.empty() ? "-" : where;
    }
}

DiagnosticsPanel::DiagnosticsPanel(UiPreviewSession& session,
                                   PreviewViewState& view,
                                   std::function<void(const std::string& path)> openPath)
    : Session(session)
    , View(view)
    , OpenPath(std::move(openPath))
{
}

void DiagnosticsPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible, PanelStyle::Standard);
    if (!panel.IsOpen())
        return;

    if (EditorChrome::Button("clear", "Clear", { EditorUi::Px(56.0f), 0.0f }, EditorChrome::ButtonTone::Normal))
        Session.ClearDiagnostics();
    ImGui::SameLine();
    ImGui::Checkbox("info", &ShowInfo);
    ImGui::SameLine();
    std::size_t errors = 0;
    std::size_t warnings = 0;
    for (const UiDiagnostic& d : Session.Diagnostics())
    {
        errors += d.Severity == UiDiagnosticSeverity::Error;
        warnings += d.Severity == UiDiagnosticSeverity::Warning;
    }
    ImGui::TextDisabled("%zu errors, %zu warnings", errors, warnings);

    if (!ImGui::BeginTable("diagnostics", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg
                                                | ImGuiTableFlags_ScrollY))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("src", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(52.0f));
    ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(120.0f));
    ImGui::TableSetupColumn("message", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("where", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(110.0f));
    ImGui::TableHeadersRow();
    for (const UiDiagnostic& d : Session.Diagnostics())
    {
        if (d.Severity == UiDiagnosticSeverity::Info && !ShowInfo)
            continue;
        ImGui::PushID(static_cast<int>(d.Sequence));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(SeverityColor(d.Severity), "%s", SourceLabel(d.Source));
        ImGui::TableNextColumn();
        ImGui::TextColored(SeverityColor(d.Severity), "%s", KindLabel(d.Kind));
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", d.Message.c_str());
        if (d.Kind == UiDiagnosticKind::RebuildFailed && ImGui::IsItemHovered())
            ImGui::SetTooltip("The previous document is still up.");
        ImGui::TableNextColumn();
        const std::string where = Where(d);
        ImGui::TextUnformatted(where.c_str());
        ImGui::TableNextColumn();
        if (IsBindingKind(d.Kind) && ImGui::SmallButton("bindings"))
            View.ShowBindings = true;
        if (d.Path && OpenPath)
        {
            if (IsBindingKind(d.Kind))
                ImGui::SameLine();
            if (ImGui::SmallButton("open"))
                OpenPath(*d.Path);
        }
        ImGui::PopID();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndTable();
}
