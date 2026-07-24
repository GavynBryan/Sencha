#pragma once

#include "DocumentCookRequest.h"

#include <assets/cook/CookedCache.h>
#include <core/json/JsonValue.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class DocumentArtifactCatalog;

// How one output family reaches the next publication:
//  Produced  - this cook baked (or step-reused) it: a fresh artifact + reference.
//  Preserved - not produced, disposition Preserve, a prior artifact exists: the
//              prior published artifact is carried forward and re-referenced.
//  Absent    - never produced and nothing to preserve.
// Withdrawal is orthogonal (see the Withdraw* flags): a family can publish
// nothing (Absent) while a superseded prior artifact is still removed on commit.
enum class FamilyPublication
{
    Absent,
    Produced,
    Preserved,
};

// The per-family publication decision for one document cook, resolved before
// scene assembly so scene assembly, index staging, and receipt construction all
// consume one value. Preserved families carry the prior published artifact by its
// stored path, so a preserved lightmap stays referenced by the cooked scene
// instead of being orphaned on disk.
struct DocumentPublicationPlan
{
    FamilyPublication Collision = FamilyPublication::Absent;
    FamilyPublication DirectLightmap = FamilyPublication::Absent;
    FamilyPublication AmbientOcclusion = FamilyPublication::Absent;
    FamilyPublication IrradianceProbes = FamilyPublication::Absent;

    // Prior baked artifacts to remove because the profile withdraws the family.
    // Independent of the publication state above: withdrawal deletes a stale
    // output whether or not this cook publishes anything for that family.
    bool WithdrawDirect = false;
    bool WithdrawAo = false;
    bool WithdrawProbe = false;

    std::optional<CookedArtifact> PreservedDirect;   // .../lightmap.stex
    std::optional<CookedArtifact> PreservedAo;        // .../ao.stex
    std::optional<CookedArtifact> PreservedProbe;     // .../probes.sprobe
    std::vector<CookedArtifact>   PreservedCollision; // per-cell .scol blobs

    [[nodiscard]] bool PreservesLighting() const
    {
        return DirectLightmap == FamilyPublication::Preserved;
    }

    // Fails with a precise diagnostic if any artifact the plan promises to
    // preserve is missing on disk, before the cook stages anything.
    [[nodiscard]] bool ValidatePreserved(const std::filesystem::path& assetsRoot,
                                         std::string* error) const;
};

// Resolves the plan from what this cook actually bakes and the active published
// entry (`priorEntry`, or null on a cold cache). AO rides with direct: the two
// share the ZoneLightmap entity, so AO is produced or preserved only alongside a
// produced or preserved direct atlas.
[[nodiscard]] DocumentPublicationPlan ResolveDocumentPublicationPlan(
    const DocumentCookRequest& request,
    bool directProduced, bool aoProduced, bool probesProduced, bool collisionProduced,
    const CookedSourceEntry* priorEntry);

// Applies the plan's preserved outputs into the assembling cook: re-emits the
// ZoneLightmap entity referencing the preserved atlas (and AO plane) and records
// every preserved texture, probe, and collision artifact in the catalog so the
// index and receipt carry them forward. No-op for families the plan does not
// preserve.
void ApplyPreservedPublication(const DocumentPublicationPlan& plan,
                               DocumentArtifactCatalog& catalog,
                               JsonValue::Array& cellEntities);
