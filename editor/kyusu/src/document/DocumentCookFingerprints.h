#pragma once

#include "DocumentCookSnapshot.h"

#include <assets/cook/ProbeBake.h>

#include <cstdint>
#include <span>
#include <string_view>

// One named identity per cook step plus the document publication identity.
// Constructed once, before cache resolution; execution reads these values and
// never recomputes a hash. Dependencies compose in resolved graph order, so a
// change to one input invalidates exactly the steps that depend on it.
struct DocumentCookFingerprints
{
    std::uint64_t Brush = 0;
    std::uint64_t Placements = 0;
    std::uint64_t LightmapSurfaces = 0;
    std::uint64_t Occlusion = 0;
    std::uint64_t Direct = 0;
    std::uint64_t Ao = 0;
    std::uint64_t Probe = 0;
    std::uint64_t Document = 0; // the cooked-cache key
};

// `reachableHalo` is the neighbor-zone occluder set within probe-ray reach,
// resolved by the caller (the occlusion BVH consumes the same set).
[[nodiscard]] DocumentCookFingerprints ComputeDocumentCookFingerprints(
    const DocumentCookSnapshot& snapshot,
    double cellSize,
    std::span<const ProbeHaloZone* const> reachableHalo);

// The named fingerprint for a cook step, so a receipt can pair a graph
// dependency edge with the input hash that gates its reuse. Steps without a
// named fingerprint (structure, collision, scene, publication) return 0.
[[nodiscard]] std::uint64_t DocumentCookStepFingerprint(
    const DocumentCookFingerprints& fingerprints, std::string_view stepId);
