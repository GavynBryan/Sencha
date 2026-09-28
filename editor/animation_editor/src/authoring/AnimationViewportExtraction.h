#pragma once

#include "authoring/AnimationNavigation.h"
#include "render/AnimationPreviewScene.h"

#include <anim/AnimPoseEvaluation.h>
#include <core/assets/AssetLease.h>
#include <math/Mat.h>
#include <render/Material.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class AnimationAuditionSelection;
class AnimationPreviewSession;
struct AnimationPoseTake;
struct RuntimeAssets;

enum class AnimationViewportSource : std::uint8_t
{
    Audition,
    Simulation,
};

// Viewport-only layer visibility; never reaches the rig or the simulation.
struct AnimationLayerDisplay
{
    std::uint8_t Muted = 0;
    std::uint8_t Soloed = 0;

    [[nodiscard]] bool Shows(std::size_t layer) const
    {
        const auto bit = static_cast<std::uint8_t>(1u << layer);
        return (Muted & bit) == 0 && (Soloed == 0 || (Soloed & bit) != 0);
    }
};

// With every layer shown this is the pose pass's own result; otherwise the
// shown layers are recomposed into `out` without touching the pass's state.
void AnimationPreviewDisplayPose(const AnimPoseSources& sources, const AnimPosePool::Slot& slot,
                                 const AnimPoseState& state, const AnimSelectorState* selection,
                                 const AnimationLayerDisplay& display, AnimTick tick, double tickSeconds,
                                 AnimPoseScratch& scratch, std::vector<Transform3f>& out);

// The tick the viewport shows: the inspected record's, else the latest.
[[nodiscard]] std::optional<AnimTick> ShownAnimationTick(const AnimationPreviewSession& simulation,
                                                         const AnimationNavigation& navigation);

// Builds the viewport's render scene from the audition or the simulation as
// they stand; it reads their clocks and never advances them.
class AnimationViewportExtraction
{
public:
    explicit AnimationViewportExtraction(RuntimeAssets& assets);

    AnimationViewportExtraction(const AnimationViewportExtraction&) = delete;
    AnimationViewportExtraction& operator=(const AnimationViewportExtraction&) = delete;
    AnimationViewportExtraction(AnimationViewportExtraction&&) = delete;
    AnimationViewportExtraction& operator=(AnimationViewportExtraction&&) = delete;

    void Extract(AnimationAuditionSelection& audition, const AnimationPreviewSession& simulation,
                 const AnimationNavigation& navigation, const AnimationPoseTake* takeA);

    // One model-space transform per joint, as last extracted.
    [[nodiscard]] const std::vector<Mat4>& Model() const { return ModelTransforms; }

    AnimationPreviewScene Scene;
    AnimationViewportSource Source = AnimationViewportSource::Audition;
    AnimationLayerDisplay LayerDisplay;
    bool ShowGhost = true;
    std::string Note;

private:
    [[nodiscard]] const std::vector<Mat4>& Palette(AnimationAuditionSelection& audition,
                                                   const AnimationPreviewSession& simulation,
                                                   const AnimationNavigation& navigation);
    // Null when the ghost is not drawn.
    [[nodiscard]] const std::vector<Mat4>* GhostPalette(const AnimationAuditionSelection& audition,
                                                        const AnimationPreviewSession& simulation,
                                                        const AnimationNavigation& navigation,
                                                        const AnimationPoseTake* takeA);

    RuntimeAssets& Assets;
    MaterialHandle GhostMaterial;
    AssetLease GhostMaterialLease;
    AnimPoseScratch DisplayScratch;
    std::vector<Mat4> GhostModel;
    std::vector<Mat4> GhostPaletteScratch;
    // The shown palette placed at a moving character's transform.
    std::vector<Mat4> PlacedPalette;
    std::vector<Transform3f> SimulationLocal;
    std::vector<Mat4> SimulationModel;
    std::vector<Mat4> SimulationPalette;
    std::vector<Mat4> ModelTransforms;
};
