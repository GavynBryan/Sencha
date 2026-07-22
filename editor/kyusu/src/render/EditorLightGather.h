#pragma once

#include <render/LightSelection.h>

#include <cstdint>
#include <vector>

class EditorDocument;

struct EditorLightGather
{
    std::vector<ForwardLightCandidate> Candidates;
    std::uint64_t ContentHash = 0;
};

// Applies editor-only visibility and baked-preview filtering while producing
// the engine-owned candidate type. Ranking, budget selection, GPU packing,
// and shadow request construction happen later in SelectForwardLights.
[[nodiscard]] EditorLightGather GatherEditorLights(
    const EditorDocument& document,
    bool skipBakedDirect,
    float globalShadowSoftness);
