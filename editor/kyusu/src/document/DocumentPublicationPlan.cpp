#include "DocumentPublicationPlan.h"

#include "CookArtifactPaths.h"
#include "CookGraph.h"
#include "DocumentArtifactCatalog.h"

#include <string_view>
#include <system_error>

namespace
{
    FamilyPublication ResolveFamily(const DocumentCookRequest& request,
                                    std::string_view family, bool produced,
                                    const CookedArtifact* prior)
    {
        if (produced)
            return FamilyPublication::Produced;
        if (request.Disposition(family) == CookOutputDisposition::Withdraw)
            return FamilyPublication::Absent;
        return prior != nullptr ? FamilyPublication::Preserved : FamilyPublication::Absent;
    }
}

bool DocumentPublicationPlan::ValidatePreserved(
    const std::filesystem::path& assetsRoot, std::string* error) const
{
    const auto check = [&](const std::optional<CookedArtifact>& artifact) -> bool
    {
        if (!artifact.has_value())
            return true;
        std::error_code ec;
        if (std::filesystem::exists(assetsRoot / artifact->FileRelPath, ec))
            return true;
        if (error != nullptr)
            *error = "preserved cook artifact is missing '" + artifact->FileRelPath + "'";
        return false;
    };
    if (!check(PreservedDirect) || !check(PreservedAo) || !check(PreservedProbe))
        return false;
    for (const CookedArtifact& blob : PreservedCollision)
    {
        std::error_code ec;
        if (!std::filesystem::exists(assetsRoot / blob.FileRelPath, ec))
        {
            if (error != nullptr)
                *error = "preserved cook artifact is missing '" + blob.FileRelPath + "'";
            return false;
        }
    }
    return true;
}

DocumentPublicationPlan ResolveDocumentPublicationPlan(
    const DocumentCookRequest& request, bool directProduced, bool aoProduced,
    bool probesProduced, bool collisionProduced, const CookedSourceEntry* priorEntry)
{
    const CookedArtifact* priorDirect = nullptr;
    const CookedArtifact* priorAo = nullptr;
    const CookedArtifact* priorProbe = nullptr;
    std::vector<const CookedArtifact*> priorCollision;
    if (priorEntry != nullptr)
        for (const CookedArtifact& artifact : priorEntry->Artifacts)
        {
            if (artifact.Type == AssetType::Texture)
            {
                if (PathHasArtifactFile(artifact.Path, kLightmapAtlasFile))
                    priorDirect = &artifact;
                else if (PathHasArtifactFile(artifact.Path, kAoAtlasFile))
                    priorAo = &artifact;
            }
            else if (artifact.Type == AssetType::ProbeVolume)
                priorProbe = &artifact;
            else if (artifact.Type == AssetType::Collision)
                priorCollision.push_back(&artifact);
        }

    DocumentPublicationPlan plan;
    plan.DirectLightmap = ResolveFamily(request, CookOutputFamilies::DirectLightmap,
                                        directProduced, priorDirect);
    plan.IrradianceProbes = ResolveFamily(request, CookOutputFamilies::IrradianceProbes,
                                          probesProduced, priorProbe);
    plan.Collision = ResolveFamily(request, CookOutputFamilies::Collision,
                                   collisionProduced,
                                   priorCollision.empty() ? nullptr : priorCollision.front());

    // AO shares the ZoneLightmap entity with direct, so it produces or preserves
    // only alongside direct. A direct-only cook (AO disabled) leaves AO absent,
    // matching the atlas it emits.
    if (plan.DirectLightmap == FamilyPublication::Produced)
        plan.AmbientOcclusion = aoProduced
            ? FamilyPublication::Produced : FamilyPublication::Absent;
    else if (plan.DirectLightmap == FamilyPublication::Preserved && priorAo != nullptr
             && request.Disposition(CookOutputFamilies::AmbientOcclusion)
                != CookOutputDisposition::Withdraw)
        plan.AmbientOcclusion = FamilyPublication::Preserved;
    else
        plan.AmbientOcclusion = FamilyPublication::Absent;

    // Withdrawal is a profile disposition, resolved here so the publisher reads
    // one value instead of re-querying the profile per family.
    plan.WithdrawDirect = request.Disposition(CookOutputFamilies::DirectLightmap)
        == CookOutputDisposition::Withdraw;
    plan.WithdrawAo = request.Disposition(CookOutputFamilies::AmbientOcclusion)
        == CookOutputDisposition::Withdraw;
    plan.WithdrawProbe = request.Disposition(CookOutputFamilies::IrradianceProbes)
        == CookOutputDisposition::Withdraw;

    if (plan.DirectLightmap == FamilyPublication::Preserved)
        plan.PreservedDirect = *priorDirect;
    if (plan.AmbientOcclusion == FamilyPublication::Preserved)
        plan.PreservedAo = *priorAo;
    if (plan.IrradianceProbes == FamilyPublication::Preserved)
        plan.PreservedProbe = *priorProbe;
    if (plan.Collision == FamilyPublication::Preserved)
        for (const CookedArtifact* blob : priorCollision)
            plan.PreservedCollision.push_back(*blob);

    return plan;
}

void ApplyPreservedPublication(const DocumentPublicationPlan& plan,
                               DocumentArtifactCatalog& catalog,
                               JsonValue::Array& cellEntities)
{
    if (plan.DirectLightmap == FamilyPublication::Preserved)
    {
        JsonValue::Object lightmapFields{
            { "texture", JsonValue(plan.PreservedDirect->Path) },
        };
        catalog.AddPreservedTexture(plan.PreservedDirect->Path,
                                    plan.PreservedDirect->FileRelPath);
        if (plan.AmbientOcclusion == FamilyPublication::Preserved)
        {
            lightmapFields.push_back({ "ao", JsonValue(plan.PreservedAo->Path) });
            catalog.AddPreservedTexture(plan.PreservedAo->Path,
                                        plan.PreservedAo->FileRelPath);
        }
        cellEntities.push_back(JsonValue(JsonValue::Object{
            { "components", JsonValue(JsonValue::Object{
                { "ZoneLightmap", JsonValue(std::move(lightmapFields)) },
            }) },
        }));
    }

    if (plan.IrradianceProbes == FamilyPublication::Preserved)
        catalog.AddPreserved(*plan.PreservedProbe);
    for (const CookedArtifact& blob : plan.PreservedCollision)
        catalog.AddPreserved(blob);
}
