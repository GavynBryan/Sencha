#include "ui/AnimationRequestSchemaPanel.h"

#include "data/DataDocumentSet.h"
#include "documents/DocumentSourceSet.h"
#include "ui/DocumentSaveReportView.h"
#include "ui/ScopedPanel.h"

#include <anim/AnimFlowData.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimRigData.h>
#include <anim/AnimSelectorData.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace
{
void TextField(const char* label, JsonValue& value, FieldEdit& edit)
{
    if (!value.IsString())
    {
        ImGui::TextWrapped("%s has an invalid value kind. Repair the schema diagnostic first.", label);
        return;
    }
    // Retain the complete authored string, including values longer than the
    // usual input widget budget. ImGui owns active text between frames.
    std::vector<char> buffer(std::max<std::size_t>(256, value.AsString().size() + 256), '\0');
    std::memcpy(buffer.data(), value.AsString().data(), value.AsString().size());
    if (ImGui::InputText(label, buffer.data(), buffer.size()))
    {
        value = JsonValue(buffer.data());
        edit.Changed = true;
    }
    edit.Committed |= ImGui::IsItemDeactivatedAfterEdit();
}

JsonValue NewParam()
{
    return JsonValue(JsonValue::Object{{"name", JsonValue("parameter")}, {"kind", JsonValue("float")}});
}

void DrawIntents(JsonValue::Array& intents, FieldEdit& edit)
{
    for (std::size_t i = 0; i < intents.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        auto& intent = intents[i];
        auto* name = intent.Find("intent");
        const auto title = name && name->IsString() ? name->AsString() : "Invalid intent";
        if (ImGui::TreeNode("intent", "%s", title.c_str()))
        {
            if (name) TextField("Intent tag", *name, edit);
            auto* params = intent.Find("params");
            if (params && params->IsArray())
            {
                auto& items = params->AsArray();
                ImGui::Text("Parameter budget: %zu / 4", items.size());
                for (std::size_t p = 0; p < items.size(); ++p)
                {
                    ImGui::PushID(static_cast<int>(p));
                    if (auto* paramName = items[p].Find("name")) TextField("Name", *paramName, edit);
                    if (auto* kind = items[p].Find("kind"); kind && kind->IsString())
                    {
                        if (ImGui::BeginCombo("Kind", kind->AsString().c_str()))
                        {
                            for (const char* option : {"float", "int", "bool", "tag"})
                                if (ImGui::Selectable(option, kind->AsString() == option))
                                {
                                    *kind = JsonValue(option);
                                    edit = {true, true};
                                }
                            ImGui::EndCombo();
                        }
                    }
                    const bool remove = ImGui::Button("Remove parameter");
                    ImGui::Separator();
                    ImGui::PopID();
                    if (remove)
                    {
                        items.erase(items.begin() + static_cast<std::ptrdiff_t>(p));
                        edit = {true, true};
                        break;
                    }
                }
                ImGui::BeginDisabled(items.size() >= 4);
                if (ImGui::Button("Add parameter")) { items.push_back(NewParam()); edit = {true, true}; }
                ImGui::EndDisabled();
            }
            const bool remove = ImGui::Button("Remove intent");
            ImGui::TreePop();
            if (remove)
            {
                intents.erase(intents.begin() + static_cast<std::ptrdiff_t>(i));
                edit = {true, true};
                ImGui::PopID();
                break;
            }
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Add intent"))
    {
        intents.emplace_back(JsonValue::Object{{"intent", JsonValue("Anim.NewIntent")},
                                               {"params", JsonValue(JsonValue::Array{})}});
        edit = {true, true};
    }
}
}

void AnimationRequestSchemaPanel::OnDraw()
{
    if (!IsVisible()) { Sources.CancelEdits(); return; }
    ScopedPanel panel(GetWindowName(), &Visible);
    if (!panel.IsOpen()) { Sources.CancelEdits(); return; }
    ImGui::TextWrapped("Author intent contracts here; preview requests and gameplay state are separate. Create new schemas from the Document panel's New asset, then open them from the content browser.");
    const auto documents = Documents.Documents();
    if (documents.empty()) return;
    const std::size_t active = Documents.ActiveIndex();
    if (ImGui::BeginCombo("Document", documents[active]->VirtualPath().c_str()))
    {
        for (std::size_t i = 0; i < documents.size(); ++i)
            if (ImGui::Selectable(documents[i]->VirtualPath().c_str(), i == active))
                Documents.SetActive(i);
        ImGui::EndCombo();
    }
    auto& document = *documents[Documents.ActiveIndex()];
    if (document.Subtype() != kAnimRequestSchemaType)
    {
        if (document.Subtype() == kAnimSelectorType)
            ImGui::TextWrapped("The active document is a selector; the Rules pane edits it.");
        else if (document.Subtype() == kAnimFlowType || document.Subtype() == kAnimRigType)
            ImGui::TextWrapped("The active document is a %s; the %s pane edits it.", document.Subtype().c_str(),
                               document.Subtype() == kAnimFlowType ? "Flow" : "Skeleton and masks");
        else
            ImGui::TextWrapped("The active document is a %s: edit it in the Document panel.",
                               document.Subtype().c_str());
        return;
    }
    ImGui::PushID(&document);
    if (ImGui::Button("Undo")) Sources.Undo();
    ImGui::SameLine();
    if (ImGui::Button("Redo")) Sources.Redo();
    ImGui::SameLine();
    if (ImGui::Button("Save")) Message = DescribeDocumentSave(Sources.Save(Documents.RefOf(document)));
    ImGui::SameLine();
    if (ImGui::Button("Reload")) (void)Documents.Store().Reload(document, Message);
    if (document.IsDirty()) ImGui::TextDisabled("Unsaved changes (save before closing the application)");
    if (document.IsExternallyModified()) ImGui::TextWrapped("Changed externally: reload a clean document to adopt disk changes.");
    if (!Message.empty()) ImGui::TextWrapped("%s", Message.c_str());

    auto root = document.CopyRoot();
    auto* data = root.Find("data");
    auto* intents = data ? data->Find("intents") : nullptr;
    FieldEdit edit;
    if (intents && intents->IsArray()) DrawIntents(intents->AsArray(), edit);
    ApplyFieldEdit(document, Documents, edit, std::move(root));
    for (const auto& error : document.ValidationErrors())
        ImGui::TextWrapped("%s: %s", error.Path.c_str(), error.Message.c_str());
    ImGui::PopID();
}
