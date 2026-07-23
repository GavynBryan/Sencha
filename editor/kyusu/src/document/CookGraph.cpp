#include "CookGraph.h"

#include <algorithm>
#include <array>
#include <unordered_set>

namespace
{
    constexpr std::array<std::string_view, 0> kNoDependencies{};
    constexpr std::array kBrushCellsDependencies{
        DocumentCookStepIds::BrushCells,
    };
    // Direct light and AO both consume the surface samples and the occlusion
    // geometry, and nothing else across steps; neither depends on the other.
    constexpr std::array kLightmapEvaluationDependencies{
        DocumentCookStepIds::LightmapSurfaces,
        DocumentCookStepIds::OcclusionGeometry,
    };
    constexpr std::array kOcclusionDependencies{
        DocumentCookStepIds::OcclusionGeometry,
    };
    constexpr std::array kSceneDependencies{
        CookStepIds::RenderMeshes,
    };
    constexpr std::array kPublicationDependencies{
        DocumentCookStepIds::CookedScene,
    };

    const std::array<CookStepDefinition, 11> kSteps{
        CookStepDefinition{
            DocumentCookStepIds::BrushCells, kNoDependencies, {}, 1, false },
        CookStepDefinition{
            DocumentCookStepIds::LightmapSurfaces, kBrushCellsDependencies, {}, 1, false },
        CookStepDefinition{
            CookStepIds::RenderMeshes, kBrushCellsDependencies,
            CookOutputFamilies::Structure, 1, true },
        CookStepDefinition{
            CookStepIds::Collision, kBrushCellsDependencies,
            CookOutputFamilies::Collision, 1, true },
        CookStepDefinition{
            DocumentCookStepIds::OcclusionGeometry, kBrushCellsDependencies, {}, 1, false },
        CookStepDefinition{
            CookStepIds::DirectLightmap,
            std::span<const std::string_view>(kLightmapEvaluationDependencies),
            CookOutputFamilies::DirectLightmap, 1, true },
        CookStepDefinition{
            CookStepIds::AmbientOcclusion,
            std::span<const std::string_view>(kLightmapEvaluationDependencies),
            CookOutputFamilies::AmbientOcclusion, 2, true },
        CookStepDefinition{
            CookStepIds::IrradianceProbes,
            std::span<const std::string_view>(kOcclusionDependencies),
            CookOutputFamilies::IrradianceProbes, 1, true },
        CookStepDefinition{
            CookStepIds::ReferencedAssets, kNoDependencies,
            CookOutputFamilies::ReferencedAssets, 1, true },
        CookStepDefinition{
            DocumentCookStepIds::CookedScene,
            std::span<const std::string_view>(kSceneDependencies), {}, 1, false },
        CookStepDefinition{
            DocumentCookStepIds::Publication,
            std::span<const std::string_view>(kPublicationDependencies), {}, 1, false },
    };
}

std::span<const CookStepDefinition> DocumentCookSteps()
{
    return kSteps;
}

const CookStepDefinition* FindDocumentCookStep(std::string_view id)
{
    const std::span<const CookStepDefinition> steps = DocumentCookSteps();
    const auto found = std::find_if(steps.begin(), steps.end(),
        [id](const CookStepDefinition& step) { return step.Id == id; });
    return found != steps.end() ? &*found : nullptr;
}

CookOutputDisposition CookProfileOutputDisposition(const CookProfile& profile,
                                                   std::string_view family)
{
    const auto found = std::find_if(profile.OutputPolicies.begin(),
        profile.OutputPolicies.end(),
        [family](const CookOutputPolicy& policy) { return policy.Family == family; });
    return found != profile.OutputPolicies.end()
        ? found->Disposition
        : CookOutputDisposition::Preserve;
}

ResolvedCookGraph ResolveDocumentCookGraph(const CookProfile& profile)
{
    ResolvedCookGraph result;
    if (profile.Id.empty() || profile.Name.empty())
    {
        result.Error = "cook profile requires an id and name";
        return result;
    }

    std::unordered_set<std::string> targetSet;
    for (const std::string& target : profile.TargetSteps)
    {
        const CookStepDefinition* definition = FindDocumentCookStep(target);
        if (definition == nullptr || !definition->ProfileSelectable)
        {
            result.Error = "unknown or non-selectable cook step '" + target + "'";
            return result;
        }
        targetSet.insert(target);
    }
    for (const CookOutputPolicy& policy : profile.OutputPolicies)
    {
        const bool known = std::ranges::any_of(DocumentCookSteps(),
            [&policy](const CookStepDefinition& step)
            { return !step.OutputFamily.empty() && step.OutputFamily == policy.Family; });
        if (!known)
        {
            result.Error = "unknown cook output family '" + policy.Family + "'";
            return result;
        }
    }

    std::unordered_set<std::string> included;
    const auto include = [&](auto&& self, std::string_view id) -> bool
    {
        if (included.contains(std::string(id)))
            return true;
        const CookStepDefinition* definition = FindDocumentCookStep(id);
        if (definition == nullptr)
        {
            result.Error = "cook graph dependency '" + std::string(id) + "' is not registered";
            return false;
        }
        for (std::string_view dependency : definition->Dependencies)
            if (!self(self, dependency))
                return false;
        included.insert(std::string(id));
        result.OrderedSteps.push_back(std::string(id));
        return true;
    };

    for (const std::string& target : profile.TargetSteps)
        if (!include(include, target))
            return result;
    if (!include(include, DocumentCookStepIds::CookedScene)
        || !include(include, DocumentCookStepIds::Publication))
        return result;

    for (const std::string& step : result.OrderedSteps)
        if (!targetSet.contains(step)
            && step != DocumentCookStepIds::CookedScene
            && step != DocumentCookStepIds::Publication)
            result.AddedPrerequisites.push_back(step);

    result.Valid = true;
    return result;
}
