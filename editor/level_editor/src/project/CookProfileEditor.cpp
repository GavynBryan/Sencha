#include "project/CookProfileEditor.h"

#include "document/CookGraph.h"
#include "project/CookProfile.h"
#include "project/Project.h"
#include "properties/SettingProperties.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>

namespace
{
constexpr std::array<std::string_view, 6> kSteps{
    CookStepIds::RenderMeshes,     CookStepIds::Collision,        CookStepIds::ReferencedAssets,
    CookStepIds::DirectLightmap,   CookStepIds::AmbientOcclusion, CookStepIds::IrradianceProbes,
};

constexpr std::array<std::string_view, 6> kOutputs{
    CookOutputFamilies::Structure,      CookOutputFamilies::Collision,        CookOutputFamilies::ReferencedAssets,
    CookOutputFamilies::DirectLightmap, CookOutputFamilies::AmbientOcclusion, CookOutputFamilies::IrradianceProbes,
};

// In CookOutputDisposition's order.
constexpr std::array<std::string_view, 3> kDispositions{ "Publish", "Preserve", "Withdraw" };

constexpr std::string_view kStepPrefix = "step.";
constexpr std::string_view kOutputPrefix = "output.";

std::string Label(std::string_view id)
{
    if (id == CookStepIds::RenderMeshes || id == CookOutputFamilies::Structure)
        return "Structure";
    if (id == CookStepIds::Collision)
        return "Collision";
    if (id == CookStepIds::ReferencedAssets)
        return "Referenced assets";
    if (id == CookStepIds::DirectLightmap)
        return "Direct lightmap";
    if (id == CookStepIds::AmbientOcclusion)
        return "Ambient occlusion";
    if (id == CookStepIds::IrradianceProbes)
        return "Irradiance probes";
    return std::string(id);
}

CookOutputDisposition DispositionOf(const CookProfile& profile, std::string_view family)
{
    const auto policy = std::ranges::find(profile.OutputPolicies, family, &CookOutputPolicy::Family);
    return policy != profile.OutputPolicies.end() ? policy->Disposition : CookOutputDisposition::Preserve;
}

bool Runs(const CookProfile& profile, std::string_view step)
{
    return std::ranges::find(profile.TargetSteps, step) != profile.TargetSteps.end();
}

// "Nightly Copy" -> "nightly_copy": an id a project file can carry.
std::string IdStem(std::string_view name)
{
    std::string stem;
    for (const char c : name)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            stem.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else if (!stem.empty() && stem.back() != '_')
            stem.push_back('_');
    }
    while (!stem.empty() && stem.back() == '_')
        stem.pop_back();
    return stem.empty() ? std::string("profile") : stem;
}

PropertyNode Group(std::string key, std::string label, std::vector<PropertyNode> children)
{
    PropertyNode node;
    node.Key = std::move(key);
    node.Descriptor.Kind = PropertyKind::Group;
    node.Descriptor.Label = std::move(label);
    node.Children = std::move(children);
    return node;
}

void MarkReadOnly(PropertyNode& node)
{
    node.Descriptor.ReadOnly = true;
    for (PropertyNode& child : node.Children)
        MarkReadOnly(child);
}
} // namespace

std::vector<CookProfile> CookProfileEditor::Profiles() const
{
    return ResolveCookProfiles(Project.CookProfiles);
}

std::size_t CookProfileEditor::Selected() const
{
    const std::vector<CookProfile> profiles = Profiles();
    const auto found = std::ranges::find(profiles, SelectedId, &CookProfile::Id);
    return found != profiles.end() ? static_cast<std::size_t>(found - profiles.begin()) : 0;
}

void CookProfileEditor::Select(std::size_t index)
{
    const std::vector<CookProfile> profiles = Profiles();
    if (index < profiles.size())
        SelectedId = profiles[index].Id;
}

bool CookProfileEditor::SelectedIsBuiltIn() const
{
    const std::vector<CookProfile> profiles = Profiles();
    const std::size_t index = Selected();
    return index < profiles.size() && profiles[index].BuiltIn;
}

CookProfile* CookProfileEditor::Owned()
{
    const auto found = std::ranges::find(Project.CookProfiles, SelectedId, &CookProfile::Id);
    return found != Project.CookProfiles.end() ? &*found : nullptr;
}

void CookProfileEditor::Duplicate()
{
    const std::vector<CookProfile> profiles = Profiles();
    CookProfile copy = profiles.empty() ? BuiltInCookProfiles().front() : profiles[Selected()];
    copy.BuiltIn = false;
    copy.Name += " Copy";
    const std::string stem = IdStem(copy.Name);
    copy.Id = stem;
    for (std::uint32_t suffix = 2; std::ranges::find(profiles, copy.Id, &CookProfile::Id) != profiles.end(); ++suffix)
        copy.Id = stem + "_" + std::to_string(suffix);
    SelectedId = copy.Id;
    Project.CookProfiles.push_back(std::move(copy));
    Dirty = true;
    ++Changes;
    Message = Problem();
}

bool CookProfileEditor::Delete()
{
    if (Owned() == nullptr)
        return false;
    std::erase_if(Project.CookProfiles, [this](const CookProfile& profile) { return profile.Id == SelectedId; });
    SelectedId = "full";
    Dirty = true;
    ++Changes;
    Message = Problem();
    return true;
}

std::string CookProfileEditor::Problem() const
{
    std::vector<std::string> ids;
    for (const CookProfile& profile : Profiles())
    {
        if (std::ranges::find(ids, profile.Id) != ids.end())
            return "Duplicate profile id '" + profile.Id + "'";
        ids.push_back(profile.Id);
        const ResolvedCookGraph graph = ResolveDocumentCookGraph(profile);
        if (!graph.Valid)
            return profile.Name + ": " + graph.Error;
    }
    return {};
}

bool CookProfileEditor::Validate()
{
    Message = Problem();
    if (!Message.empty())
        return false;
    Message = "Profiles are valid.";
    return true;
}

bool CookProfileEditor::Save()
{
    if (!Validate())
        return false;
    const std::filesystem::path path = std::filesystem::path(Project.Directory) / "project.senchaproj";
    std::string error;
    if (!Project.Save(path.string(), &error))
    {
        Message = error;
        return false;
    }
    Dirty = false;
    Message = "Saved " + path.generic_string();
    return true;
}

std::vector<PropertyNode> CookProfileEditor::Nodes() const
{
    const std::vector<CookProfile> profiles = Profiles();
    if (profiles.empty())
        return {};
    const CookProfile& profile = profiles[Selected()];

    PropertyNode name;
    name.Key = "name";
    name.Descriptor.Kind = PropertyKind::Text;
    name.Descriptor.Label = "Name";
    name.Value = profile.Name;

    std::vector<PropertyNode> steps;
    for (const std::string_view step : kSteps)
        steps.push_back(ToggleProperty(std::string(kStepPrefix) + std::string(step), Label(step), Runs(profile, step)));
    std::vector<PropertyNode> outputs;
    for (const std::string_view family : kOutputs)
        outputs.push_back(ChoiceProperty(std::string(kOutputPrefix) + std::string(family), Label(family),
                                         kDispositions, static_cast<std::size_t>(DispositionOf(profile, family))));

    std::vector<PropertyNode> nodes{ std::move(name), Group("steps", "Steps", std::move(steps)),
                                     Group("outputs", "Published outputs", std::move(outputs)) };
    if (profile.BuiltIn)
        for (PropertyNode& node : nodes)
            MarkReadOnly(node);
    return nodes;
}

std::string CookProfileEditor::ValueOf(std::string_view key) const
{
    for (const PropertyNode& node : Nodes())
    {
        if (node.Key == key)
            return node.Value;
        for (const PropertyNode& child : node.Children)
            if (child.Key == key)
                return child.Value;
    }
    return {};
}

bool CookProfileEditor::BeginEdit(std::string_view key)
{
    if (SelectedIsBuiltIn())
        return false;
    EditKey = std::string(key);
    EditBegan = ValueOf(key);
    return true;
}

PropertyEditResult CookProfileEditor::PreviewEdit(std::string_view key, std::string_view value)
{
    return Set(key, value);
}

PropertyEditResult CookProfileEditor::CommitEdit(std::string_view key, std::string_view value)
{
    const PropertyEditResult result = Set(key, value);
    EditKey.clear();
    return result;
}

void CookProfileEditor::CancelEdit(std::string_view key)
{
    if (key == EditKey)
        (void)Set(key, EditBegan);
    EditKey.clear();
}

PropertyEditResult CookProfileEditor::Set(std::string_view key, std::string_view value)
{
    CookProfile* profile = Owned();
    if (profile == nullptr)
        return PropertyEditResult::Refused;
    if (value == ValueOf(key))
        return PropertyEditResult::Unchanged;

    if (key == "name")
    {
        if (value.empty())
            return PropertyEditResult::Refused;
        profile->Name = std::string(value);
    }
    else if (key.starts_with(kStepPrefix))
    {
        const std::string_view step = key.substr(kStepPrefix.size());
        const std::optional<bool> on = ParseToggle(value);
        if (!on.has_value() || std::ranges::find(kSteps, step) == kSteps.end())
            return PropertyEditResult::Refused;
        if (*on)
            profile->TargetSteps.emplace_back(step);
        else
            std::erase(profile->TargetSteps, step);
    }
    else if (key.starts_with(kOutputPrefix))
    {
        const std::string_view family = key.substr(kOutputPrefix.size());
        const std::optional<std::size_t> choice = ParseChoice(value, kDispositions);
        if (!choice.has_value() || std::ranges::find(kOutputs, family) == kOutputs.end())
            return PropertyEditResult::Refused;
        const auto disposition = static_cast<CookOutputDisposition>(*choice);
        const auto policy = std::ranges::find(profile->OutputPolicies, family, &CookOutputPolicy::Family);
        if (policy != profile->OutputPolicies.end())
            policy->Disposition = disposition;
        else
            profile->OutputPolicies.push_back({ std::string(family), disposition });
    }
    else
    {
        return PropertyEditResult::Gone;
    }
    Dirty = true;
    ++Changes;
    Message = Problem();
    return PropertyEditResult::Applied;
}
