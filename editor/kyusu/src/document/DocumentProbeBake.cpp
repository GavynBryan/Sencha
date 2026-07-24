#include "DocumentProbeBake.h"

#include "CookArtifactPaths.h"
#include "CookArtifactTransaction.h"
#include "DocumentCookContext.h"
#include "CookStepCache.h"
#include "CookStepProgress.h"
#include "DocumentArtifactCatalog.h"
#include "DocumentCookReuse.h"

#include <assets/cook/ProbeBake.h>
#include <assets/probes/ProbeVolumeFormat.h>
#include <core/serialization/BinaryWriter.h>
#include <project/CookProfile.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

bool BakeDocumentProbes(const DocumentCookContext& ctx,
                        const DocumentCookSnapshot& snapshot,
                        const BakeBvh& occlusionBvh, const DocumentCookReuse& reuse,
                        std::optional<CookedArtifact>& probeArtifact)
{
    const std::filesystem::path& assetsRoot = ctx.AssetsRoot;
    CookArtifactTransaction& transaction = ctx.Transaction;
    DocumentArtifactCatalog& catalog = ctx.Catalog;
    CookStepProgress& progress = ctx.Progress;
    DocumentCookResult& result = ctx.Result;
    const LightingCookParams& params = snapshot.Lighting;
    const std::string stemStr(ctx.Stem());
    progress.Begin(CookStepIds::IrradianceProbes);
    if (reuse.Probes != nullptr)
    {
        std::string restoreError;
        CookedArtifact restoredProbe;
        if (!catalog.RestoreStep(*reuse.Probes, assetsRoot, transaction, false,
                                 restoredProbe, &restoreError))
        {
            result.Error = "CookDocument: " + restoreError;
            return false;
        }
        probeArtifact = restoredProbe;
        result.ReusedSteps.push_back(std::string(CookStepIds::IrradianceProbes));
        RestoreDocumentCookResultMetadata(reuse.Probes->Metadata, result);
        progress.Complete();
        return true;
    }

    ProbeBakeParams probeParams = params.Probe;
    probeParams.Shading = params.Shading;
    const std::vector<Vec3d> rayTable = BuildProbeRayTable(params.ProbeRayCount);

    ProbeVolumeFile probeFile;
    std::size_t probeCount = 0;
    for (std::size_t i = 0; i < snapshot.ProbeVolumes.size(); ++i)
    {
        if (progress.Cancelled(result))
            return false;
        const ProbeVolumeInput& input = snapshot.ProbeVolumes[i];
        const ProbeVolumeBakeResult baked = BakeProbeVolume(
            input.Grid, occlusionBvh, snapshot.BounceLights, rayTable, probeParams);

        ProbeVolumeRecord record;
        record.Grid = input.Grid;
        record.Priority = input.Priority;
        record.StableIndex = static_cast<std::uint32_t>(i);
        record.ShHalf = PackProbeShHalf(baked.Sh);
        record.ValidityBits = PackValidityBits(baked.Valid);
        probeCount += baked.Sh.size();
        probeFile.Volumes.push_back(std::move(record));
    }

    const std::string probeRel = ProbeVolumeRel(stemStr);
    const std::filesystem::path probePhysical = assetsRoot / ".cooked" / probeRel;
    const std::filesystem::path probeStaged = transaction.Stage(probePhysical);
    std::error_code makeDirs;
    std::filesystem::create_directories(probeStaged.parent_path(), makeDirs);
    std::ofstream probeStream(probeStaged, std::ios::binary | std::ios::trunc);
    BinaryWriter probeWriter(probeStream);
    if (!probeStream.is_open() || !WriteProbeVolumeFile(probeWriter, probeFile))
    {
        result.Error = "CookDocument: could not write probe volumes '" + probeRel + "'";
        return false;
    }
    probeArtifact = catalog.AddProbeVolume("asset://" + probeRel, ".cooked/" + probeRel);
    result.ProbeVolumeCount = snapshot.ProbeVolumes.size();
    result.ProbeCount = probeCount;
    progress.Complete();
    return true;
}
