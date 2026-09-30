#include "DocumentLibraryPanel.h"

#include "icons/IconId.h"
#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "ui/chrome/ChromeBars.h"
#include "ui/chrome/ChromeControls.h"
#include "ui/chrome/ChromeHeader.h"

#include <imgui.h>

#include <cstring>
#include <string_view>

DocumentLibraryPanel::DocumentLibraryPanel(DocumentLibrary& library, UiPreviewSession& session, Actions actions)
    : Library(library)
    , Session(session)
    , Act(std::move(actions))
{
}

void DocumentLibraryPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible, PanelStyle::Tool);
    if (!panel.IsOpen())
        return;

    const float button = EditorChrome::BarButtonSize();
    if (EditorChrome::ToolButton("rescan", IconId::Refresh, "Rescan the libraries for documents (Ctrl+R)", false,
                                 button)
        && Act.Rescan)
        Act.Rescan();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "filter", FilterBuffer, sizeof(FilterBuffer));

    if (Library.Documents().empty())
    {
        ImGui::TextDisabled("No documents. Open a project, or add .rml files to a mounted root.");
        return;
    }

    const std::string_view filter(FilterBuffer);
    std::string_view currentLibrary;
    for (const DocumentEntry& entry : Library.Documents())
    {
        if (!filter.empty() && entry.RelPath.find(filter) == std::string::npos)
            continue;
        if (entry.Library != currentLibrary)
        {
            currentLibrary = entry.Library;
            EditorChrome::SectionTitle(entry.Library.c_str());
        }
        DrawRow(entry);
    }
}

void DocumentLibraryPanel::DrawRow(const DocumentEntry& entry)
{
    const bool open = Session.IsOpen() && Session.PackagePath() == entry.PackagePath;
    ImGui::PushID(entry.PackagePath.c_str());

    // The cook lamp first: a document with no cook is one the layer cannot open.
    {
        const float h = ImGui::GetTextLineHeight();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled({ pos.x + h * 0.5f, pos.y + h * 0.5f }, h * 0.22f,
                                                    ImGui::GetColorU32(entry.Cooked ? EditorUi::Accent
                                                                                    : EditorUi::Danger));
        ImGui::Dummy({ h, h });
        ImGui::SameLine();
    }
    if (ImGui::Selectable(entry.RelPath.c_str(), open) && Act.Open)
        Act.Open(entry.PackagePath);
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(entry.PackagePath.c_str());
        ImGui::Text("model: %s", entry.ModelName.empty() ? "(static)" : entry.ModelName.c_str());
        ImGui::Text("cooked: %s", entry.Cooked ? "yes" : "no");
        ImGui::Text("preview model: %s", entry.HasPreviewModel ? "yes" : "none yet");
        ImGui::EndTooltip();
    }
    if (ImGui::BeginPopupContextItem("row_context"))
    {
        if (ImGui::MenuItem("Open in editor") && Act.OpenInEditor)
            Act.OpenInEditor(entry);
        ImGui::EndPopup();
    }
    if (!entry.ModelName.empty())
    {
        ImGui::SameLine();
        EditorUi::RoleLabel(EditorUi::TextRole::SecondaryText, entry.ModelName);
    }
    ImGui::PopID();
}
