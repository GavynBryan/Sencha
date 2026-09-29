#include "ui/NewDataAssetForm.h"

#include "data/DataDocumentSet.h"

#include <imgui.h>

#include <algorithm>

void NewDataAssetForm::Draw(DataDocumentSet& documents, const char* pathHint)
{
    const std::vector<std::string> subtypes = documents.CreatableSubtypes();
    if (subtypes.empty())
    {
        ImGui::TextWrapped("No data subtype with an authoring schema is registered.");
        return;
    }
    Subtype = std::min(Subtype, subtypes.size() - 1);
    if (ImGui::BeginCombo("Kind", subtypes[Subtype].c_str()))
    {
        for (std::size_t i = 0; i < subtypes.size(); ++i)
            if (ImGui::Selectable(subtypes[i].c_str(), i == Subtype))
                Subtype = i;
        ImGui::EndCombo();
    }
    ImGui::InputTextWithHint("Path", pathHint, Path.data(), Path.size());
    if (ImGui::Button("Create") && Path[0] != '\0')
    {
        Error.clear();
        if (documents.Create(subtypes[Subtype], Path.data(), Error) != nullptr)
            Path.fill('\0');
    }
    if (!Error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", Error.c_str());
}
