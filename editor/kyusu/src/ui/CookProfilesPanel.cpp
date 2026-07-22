#include "CookProfilesPanel.h"

#include "document/CookGraph.h"
#include "project/Project.h"
#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>

namespace
{
    constexpr std::array<std::string_view, 6> PublicSteps{
        CookStepIds::RenderMeshes,
        CookStepIds::Collision,
        CookStepIds::ReferencedAssets,
        CookStepIds::DirectLightmap,
        CookStepIds::AmbientOcclusion,
        CookStepIds::IrradianceProbes,
    };

    constexpr std::array<std::string_view, 6> OutputFamilies{
        CookOutputFamilies::Structure,
        CookOutputFamilies::Collision,
        CookOutputFamilies::ReferencedAssets,
        CookOutputFamilies::DirectLightmap,
        CookOutputFamilies::AmbientOcclusion,
        CookOutputFamilies::IrradianceProbes,
    };

    const char* Label(std::string_view id)
    {
        if (id == CookStepIds::RenderMeshes || id == CookOutputFamilies::Structure)
            return "Structure";
        if (id == CookStepIds::Collision || id == CookOutputFamilies::Collision)
            return "Collision";
        if (id == CookStepIds::ReferencedAssets || id == CookOutputFamilies::ReferencedAssets)
            return "Referenced assets";
        if (id == CookStepIds::DirectLightmap || id == CookOutputFamilies::DirectLightmap)
            return "Direct lightmap";
        if (id == CookStepIds::AmbientOcclusion || id == CookOutputFamilies::AmbientOcclusion)
            return "Ambient occlusion";
        if (id == CookStepIds::IrradianceProbes || id == CookOutputFamilies::IrradianceProbes)
            return "Irradiance probes";
        return "Unknown";
    }

    const char* DispositionLabel(CookOutputDisposition disposition)
    {
        switch (disposition)
        {
        case CookOutputDisposition::Publish: return "Publish";
        case CookOutputDisposition::Preserve: return "Preserve";
        case CookOutputDisposition::Withdraw: return "Withdraw";
        }
        return "Preserve";
    }

    CookOutputDisposition DispositionAt(int index)
    {
        return static_cast<CookOutputDisposition>(index);
    }
}

CookProfilesPanel::CookProfilesPanel(ProjectDescriptor* project)
    : Project(project)
{
}

CookProfile* CookProfilesPanel::EditableProfile()
{
    if (Project == nullptr)
        return nullptr;
    const auto found = std::find_if(
        Project->CookProfiles.begin(), Project->CookProfiles.end(),
        [this](const CookProfile& profile) { return profile.Id == SelectedId; });
    return found != Project->CookProfiles.end() ? &*found : nullptr;
}

void CookProfilesPanel::AddProfile(const CookProfile* source)
{
    if (Project == nullptr)
        return;
    CookProfile profile = source != nullptr ? *source : BuiltInCookProfiles().front();
    profile.BuiltIn = false;
    profile.Name = source != nullptr ? source->Name + " Copy" : "New Profile";

    std::string stem;
    for (char c : profile.Name)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            stem.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else if (!stem.empty() && stem.back() != '_')
            stem.push_back('_');
    }
    while (!stem.empty() && stem.back() == '_')
        stem.pop_back();
    if (stem.empty())
        stem = "profile";

    profile.Id = stem;
    const std::vector<CookProfile> existingProfiles =
        ResolveCookProfiles(Project->CookProfiles);
    for (std::uint32_t suffix = 2;; ++suffix)
    {
        const bool unique = std::none_of(
            existingProfiles.begin(), existingProfiles.end(),
            [&profile](const CookProfile& existing) { return existing.Id == profile.Id; });
        if (unique)
            break;
        profile.Id = stem + "_" + std::to_string(suffix);
    }
    SelectedId = profile.Id;
    Project->CookProfiles.push_back(std::move(profile));
    Dirty = true;
    Validate();
}

void CookProfilesPanel::Save()
{
    if (Project == nullptr)
        return;
    Validate();
    if (!Message.empty())
        return;
    std::string error;
    const std::filesystem::path path =
        std::filesystem::path(Project->Directory) / "project.senchaproj";
    if (!Project->Save(path.string(), &error))
    {
        Message = error;
        return;
    }
    Dirty = false;
    Message = "Saved " + path.generic_string();
}

void CookProfilesPanel::Validate()
{
    Message.clear();
    if (Project == nullptr)
        return;
    std::vector<std::string> ids;
    for (const CookProfile& profile : ResolveCookProfiles(Project->CookProfiles))
    {
        if (std::find(ids.begin(), ids.end(), profile.Id) != ids.end())
        {
            Message = "Duplicate profile id '" + profile.Id + "'";
            return;
        }
        ids.push_back(profile.Id);
        const ResolvedCookGraph graph = ResolveDocumentCookGraph(profile);
        if (!graph.Valid)
        {
            Message = profile.Name + ": " + graph.Error;
            return;
        }
    }
}

void CookProfilesPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;
    if (Project == nullptr)
    {
        ImGui::TextDisabled("Open a project to edit cook profiles.");
        return;
    }

    std::vector<CookProfile> profiles = ResolveCookProfiles(Project->CookProfiles);
    auto selected = std::find_if(profiles.begin(), profiles.end(),
        [this](const CookProfile& profile) { return profile.Id == SelectedId; });
    if (selected == profiles.end())
    {
        SelectedId = "full";
        selected = profiles.begin();
    }

    if (ImGui::BeginCombo("Profile", selected->Name.c_str()))
    {
        for (const CookProfile& profile : profiles)
            if (ImGui::Selectable(profile.Name.c_str(), profile.Id == SelectedId))
                SelectedId = profile.Id;
        ImGui::EndCombo();
    }

    CookProfile* editable = EditableProfile();
    const CookProfile* view = editable != nullptr ? editable : &*selected;
    if (view->BuiltIn)
        ImGui::TextDisabled("Built-in profile (read only)");
    ImGui::SameLine();
    if (ImGui::Button("Duplicate"))
    {
        AddProfile(view);
        return;
    }
    if (editable != nullptr)
    {
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
            std::erase_if(Project->CookProfiles,
                [this](const CookProfile& profile) { return profile.Id == SelectedId; });
            SelectedId = "full";
            Dirty = true;
            Validate();
            return;
        }

        std::array<char, 128> name{};
        std::snprintf(name.data(), name.size(), "%s", editable->Name.c_str());
        if (ImGui::InputText("Name", name.data(), name.size()))
        {
            editable->Name = name.data();
            Dirty = true;
            Validate();
        }
    }
    else
    {
        ImGui::Text("Name: %s", view->Name.c_str());
    }
    ImGui::TextDisabled("Id: %s", view->Id.c_str());

    ImGui::SeparatorText("Steps");
    ImGui::PushID("cook_steps");
    for (std::string_view step : PublicSteps)
    {
        bool enabled = std::find(view->TargetSteps.begin(), view->TargetSteps.end(), step)
                       != view->TargetSteps.end();
        if (editable == nullptr)
            ImGui::BeginDisabled();
        if (ImGui::Checkbox(Label(step), &enabled) && editable != nullptr)
        {
            if (enabled)
                editable->TargetSteps.push_back(std::string(step));
            else
                std::erase(editable->TargetSteps, step);
            Dirty = true;
            Validate();
        }
        if (editable == nullptr)
            ImGui::EndDisabled();
    }
    ImGui::PopID();

    ImGui::SeparatorText("Published outputs");
    ImGui::PushID("published_outputs");
    for (std::string_view family : OutputFamilies)
    {
        CookOutputDisposition disposition = CookOutputDisposition::Preserve;
        auto policy = std::find_if(view->OutputPolicies.begin(), view->OutputPolicies.end(),
            [family](const CookOutputPolicy& value) { return value.Family == family; });
        if (policy != view->OutputPolicies.end())
            disposition = policy->Disposition;
        if (editable == nullptr)
            ImGui::BeginDisabled();
        if (ImGui::BeginCombo(Label(family), DispositionLabel(disposition)))
        {
            for (int option = 0; option != 3; ++option)
            {
                const CookOutputDisposition candidate = DispositionAt(option);
                if (ImGui::Selectable(DispositionLabel(candidate), candidate == disposition) &&
                    editable != nullptr)
                {
                    auto target = std::find_if(
                        editable->OutputPolicies.begin(), editable->OutputPolicies.end(),
                        [family](const CookOutputPolicy& value) { return value.Family == family; });
                    if (target == editable->OutputPolicies.end())
                        editable->OutputPolicies.push_back({ std::string(family), candidate });
                    else
                        target->Disposition = candidate;
                    Dirty = true;
                    Validate();
                }
            }
            ImGui::EndCombo();
        }
        if (editable == nullptr)
            ImGui::EndDisabled();
    }
    ImGui::PopID();

    if (ImGui::Button(Dirty ? "Save Profiles*" : "Save Profiles"))
        Save();
    if (!Message.empty())
    {
        ImGui::SameLine();
        ImGui::TextWrapped("%s", Message.c_str());
    }
}
