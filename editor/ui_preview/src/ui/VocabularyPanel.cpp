#include "VocabularyPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "ui/chrome/ChromeControls.h"

#include <imgui.h>

#include <utility>

VocabularyPanel::VocabularyPanel(const VocabularyCatalog& catalog,
                                 std::function<std::vector<BindingAsset>()> bindingAssets)
    : Catalog(catalog)
    , BindingAssets(std::move(bindingAssets))
{
}

void VocabularyPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible, PanelStyle::Standard);
    if (!panel.IsOpen())
        return;

    // Enumerated on demand rather than per frame: the set of binding assets
    // changes when the author saves one, not while they look at the list.
    if (!Scanned
        || EditorChrome::Button("refresh", "Refresh", { EditorUi::Px(64.0f), 0.0f },
                                EditorChrome::ButtonTone::Normal))
    {
        Assets = BindingAssets ? BindingAssets() : std::vector<BindingAsset>{};
        Scanned = true;
    }
    for (const std::string& error : Catalog.Errors())
        ImGui::TextColored(EditorUi::Danger, "%s", error.c_str());

    if (ImGui::CollapsingHeader("Verbs", ImGuiTreeNodeFlags_DefaultOpen)
        && ImGui::BeginTable("verbs", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("provider", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(80.0f));
        ImGui::TableSetupColumn("args", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(40.0f));
        ImGui::TableHeadersRow();
        for (const VocabularyCatalog::VerbRow& verb : Catalog.ListVerbs())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(verb.Name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(verb.DisplayName.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", verb.Provider.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%zu", verb.ArgumentCount);
        }
        ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("Queries", ImGuiTreeNodeFlags_DefaultOpen)
        && ImGui::BeginTable("queries", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("provider", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(80.0f));
        ImGui::TableSetupColumn("args", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(40.0f));
        ImGui::TableHeadersRow();
        for (const VocabularyCatalog::QueryRow& query : Catalog.ListQueries())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(query.Name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(query.DisplayName.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", query.Provider.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%zu", query.ArgumentCount);
        }
        ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("Events", ImGuiTreeNodeFlags_DefaultOpen)
        && ImGui::BeginTable("events", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("source", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("provider", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(80.0f));
        ImGui::TableSetupColumn("payload", ImGuiTableColumnFlags_WidthFixed, EditorUi::Px(56.0f));
        ImGui::TableHeadersRow();
        for (const VocabularyCatalog::EventRow& event : Catalog.ListEvents())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(event.Name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(event.SourceComponent.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", event.Provider.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%zu", event.PayloadCount);
        }
        ImGui::EndTable();
    }

    if (!ImGui::CollapsingHeader("Bindings", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (Assets.empty())
    {
        ImGui::TextDisabled("no authored binding assets in the mounted content");
        return;
    }
    for (const BindingAsset& asset : Assets)
    {
        ImGui::PushID(asset.Path.c_str());
        ImGui::TextUnformatted(asset.Path.c_str());
        if (ImGui::BeginTable("records", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("verb", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableHeadersRow();
            for (const VocabularyCatalog::BindingRow& record : asset.Records)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(record.Key.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(record.VerbName.c_str());
                ImGui::TableNextColumn();
                if (record.Resolved && record.ReferencesChecked)
                    ImGui::TextDisabled("resolved");
                else if (record.Resolved)
                    ImGui::TextColored(EditorUi::Warning, "resolved; asset references unchecked");
                else
                    ImGui::TextColored(EditorUi::Warning, "%s", record.Error.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
}
