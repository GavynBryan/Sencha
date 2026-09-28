#include "authoring/AnimationViewportExtraction.h"

#include "authoring/AnimationAuditionSelection.h"
#include "authoring/AnimationBlendComparison.h"
#include "authoring/AnimationPreviewSession.h"

#include <anim/AnimPoseComposition.h>
#include <anim/AnimationClipSampling.h>
#include <anim/SkinningPalette.h>
#include <assets/runtime/RuntimeAssets.h>

#include <algorithm>
#include <array>
#include <format>
#include <span>

std::optional<AnimTick> ShownAnimationTick(const AnimationPreviewSession& simulation,
                                           const AnimationNavigation& navigation)
{
    const auto& history = simulation.History();
    if (history.empty())
        return std::nullopt;
    if (navigation.InspectRecord && *navigation.InspectRecord < history.size())
        return history[*navigation.InspectRecord].Tick;
    return history.back().Tick;
}

AnimationViewportExtraction::AnimationViewportExtraction(RuntimeAssets& assets)
    : Assets(assets)
{
    Material ghost;
    ghost.BaseColor = Vec4(1.0f, 0.55f, 0.15f, 0.35f);
    ghost.AlphaMode = MaterialAlphaMode::Blend;
    GhostMaterial = Assets.Materials.Create(ghost);
    GhostMaterialLease = AssetLease::Adopt(AssetType::Material, Assets.Materials, GhostMaterial.ToToken());
}

void AnimationViewportExtraction::Extract(AnimationAuditionSelection& audition, const AnimationPreviewSession& simulation,
                                          const AnimationNavigation& navigation, const AnimationPoseTake* takeA)
{
    Scene.Queue.Reset();
    Scene.Poses->Reset();
    Scene.Bounds = Aabb3d::Empty();
    if (!audition.HasMesh())
        return;
    const SkinnedMeshHandle mesh = audition.Mesh();
    const auto* geometry = Assets.SkinnedMeshes->Get(mesh);
    if (!geometry)
        return;
    Scene.Bounds = geometry->LocalBounds;
    const std::vector<Mat4>* shownPalette = &Palette(audition, simulation, navigation);
    Aabb3d drawBounds = geometry->LocalBounds;
    // Model space is the character's; its feet are half the capsule below the centre.
    if (Source == AnimationViewportSource::Simulation)
        if (const Transform3f* subjectTransform = simulation.SubjectTransform())
        {
            const Vec3d feet = subjectTransform->Position - Vec3d(0.0f, simulation.SubjectHeight() * 0.5f, 0.0f);
            const Mat4 placed = Mat4::MakeTRS(feet, Vec3d::Zero(), Vec3d::One()) * subjectTransform->Rotation.ToMat4();
            PlacedPalette.resize(shownPalette->size());
            for (std::size_t j = 0; j < PlacedPalette.size(); ++j)
                PlacedPalette[j] = placed * (*shownPalette)[j];
            shownPalette = &PlacedPalette;
            // Conservative bounds: the local box's radius around the moved centre.
            const float reach = geometry->LocalBounds.HalfExtent().Magnitude();
            drawBounds = Aabb3d::FromCenterHalfExtent(feet + geometry->LocalBounds.Center(), Vec3d(reach, reach, reach));
        }
    const auto& palette = *shownPalette;
    // A palette entry is model times inverse bind; picking needs the model part.
    const SkeletonData& shown = audition.Session.Skeleton();
    ModelTransforms.resize(std::min(palette.size(), shown.Joints.size()));
    for (std::size_t j = 0; j < ModelTransforms.size(); ++j)
        ModelTransforms[j] = palette[j] * shown.Joints[j].InverseBind.Inverse();
    // Scope is nonzero to keep this editor identity distinct from runtime entity namespaces.
    const auto slot = Scene.Poses->AppendInstance(mesh, RenderEntityKey{ .Scope = 1, .Entity = {} },
                                                 static_cast<std::uint32_t>(palette.size()));
    std::copy(palette.begin(), palette.end(), Scene.Poses->Palettes.begin() + Scene.Poses->Instances[slot].PaletteOffset);
    const MaterialHandle material = audition.Material();
    const auto* value = Assets.Materials.Get(material);
    if (!value)
        return;
    for (std::size_t section = 0; section < geometry->Sections.size(); ++section)
    {
        RenderQueueItem item;
        item.SkinnedMesh = mesh;
        item.Material = material;
        item.SectionIndex = static_cast<std::uint32_t>(section);
        item.WorldBounds = drawBounds;
        item.PoseSlot = slot;
        item.Pipeline = SelectOpaquePipeline(*value);
        item.Pass = ResolveMaterialPass(*value);
        if (item.Pass == ShaderPassId::ForwardTransparent)
            Scene.Queue.AddTransparent(item);
        else
            Scene.Queue.AddOpaque(item);
    }
    if (const std::vector<Mat4>* ghost = GhostPalette(audition, simulation, navigation, takeA);
        ghost != nullptr && ghost->size() == palette.size())
    {
        const auto ghostSlot = Scene.Poses->AppendInstance(mesh, RenderEntityKey{ .Scope = 2, .Entity = {} },
                                                          static_cast<std::uint32_t>(ghost->size()));
        std::copy(ghost->begin(), ghost->end(),
                  Scene.Poses->Palettes.begin() + Scene.Poses->Instances[ghostSlot].PaletteOffset);
        if (const auto* ghostMaterial = Assets.Materials.Get(GhostMaterial))
            for (std::size_t section = 0; section < geometry->Sections.size(); ++section)
            {
                RenderQueueItem item;
                item.SkinnedMesh = mesh;
                item.Material = GhostMaterial;
                item.SectionIndex = static_cast<std::uint32_t>(section);
                item.WorldBounds = geometry->LocalBounds;
                item.PoseSlot = ghostSlot;
                item.Pipeline = SelectOpaquePipeline(*ghostMaterial);
                item.Pass = ResolveMaterialPass(*ghostMaterial);
                Scene.Queue.AddTransparent(item);
            }
    }
    Scene.Queue.SortOpaque();
}

const std::vector<Mat4>* AnimationViewportExtraction::GhostPalette(const AnimationAuditionSelection& audition,
                                                                   const AnimationPreviewSession& simulation,
                                                                   const AnimationNavigation& navigation,
                                                                   const AnimationPoseTake* takeA)
{
    if (takeA == nullptr || !ShowGhost || Source != AnimationViewportSource::Simulation)
        return nullptr;
    const std::optional<AnimTick> tick = ShownAnimationTick(simulation, navigation);
    const std::vector<Transform3f>* pose = tick ? takeA->At(*tick) : nullptr;
    const SkeletonData& skeleton = audition.Session.Skeleton();
    if (pose == nullptr || pose->size() != skeleton.Joints.size())
        return nullptr;
    BuildPosedModelTransforms(skeleton, *pose, GhostModel);
    BuildSkinningPalette(skeleton, GhostModel, GhostPaletteScratch);
    return &GhostPaletteScratch;
}

const std::vector<Mat4>& AnimationViewportExtraction::Palette(AnimationAuditionSelection& audition,
                                                              const AnimationPreviewSession& simulation,
                                                              const AnimationNavigation& navigation)
{
    Note.clear();
    if (Source != AnimationViewportSource::Simulation || !simulation.IsOpen())
        return audition.Session.Palette();
    // Shown on the audition's skeleton only when the rig poses that skeleton.
    const SkeletonData& skeleton = audition.Session.Skeleton();
    const AnimBoundRig* rig = simulation.Rig();
    const AnimPosePool::Slot* slot = simulation.SubjectPose();
    const AnimPoseState* state = simulation.SubjectPoseState();
    if (rig == nullptr || slot == nullptr || state == nullptr || !slot->HasCurrent)
    {
        Note = rig != nullptr && rig->SkeletonPath.empty() ? "The rig names no skeleton, so nothing poses it."
                                                           : "Nothing posed yet.";
        BuildRestSkinningPalette(skeleton, SimulationPalette);
        return SimulationPalette;
    }
    if (rig->SkeletonPath != audition.Session.SkeletonPath() || skeleton.Joints.size() != slot->Joints)
    {
        Note = "The rig poses " + rig->SkeletonPath + ", not the skeleton on screen; showing the bind pose.";
        BuildRestSkinningPalette(skeleton, SimulationPalette);
        return SimulationPalette;
    }
    const AnimPoseSources sources{ rig, &Assets.AnimationClips, &skeleton };
    const auto& history = simulation.History();
    if (navigation.InspectRecord && *navigation.InspectRecord < history.size()
        && history[*navigation.InspectRecord].Pose.size() == skeleton.Joints.size())
        SimulationLocal = history[*navigation.InspectRecord].Pose;
    else
        AnimationPreviewDisplayPose(sources, *slot, *state, simulation.Selection(), LayerDisplay, slot->Tick,
                                    simulation.TickSeconds(), DisplayScratch, SimulationLocal);
    for (std::size_t l = 0; l < rig->Layers.size() && l < kAnimMaxLayers; ++l)
    {
        const std::uint16_t content = state->Layers[l].Playing.Content;
        if (content < rig->Contents.size())
            Note += std::format("{}{}: {}{}", Note.empty() ? "" : "; ", rig->Layers[l].NameText,
                                rig->Contents[content].Path, LayerDisplay.Shows(l) ? "" : " (hidden)");
    }
    BuildPosedModelTransforms(skeleton, SimulationLocal, SimulationModel);
    BuildSkinningPalette(skeleton, SimulationModel, SimulationPalette);
    return SimulationPalette;
}

void AnimationPreviewDisplayPose(const AnimPoseSources& sources, const AnimPosePool::Slot& slot,
                                 const AnimPoseState& state, const AnimSelectorState* selection,
                                 const AnimationLayerDisplay& display, AnimTick tick, double tickSeconds,
                                 AnimPoseScratch& scratch, std::vector<Transform3f>& out)
{
    const AnimBoundRig& rig = *sources.Rig;
    const std::size_t layers = std::min<std::size_t>(rig.Layers.size(), slot.Layers);
    bool everyLayer = true;
    for (std::size_t l = 0; l < layers; ++l)
        everyLayer = everyLayer && display.Shows(l);
    if (everyLayer)
    {
        out = slot.Current;
        return;
    }
    std::array<AnimPoseLayer, kAnimMaxLayers> compose{};
    for (std::size_t l = 0; l < layers; ++l)
    {
        const AnimLayerPose& layer = state.Layers[l];
        const bool plays = layer.Playing.Content != kAnimNoContent || layer.Fading;
        if (!display.Shows(l) || !plays)
            continue;
        const AnimBoundLayer& bound = rig.Layers[l];
        compose[l].Pose = slot.LayerPose(l);
        compose[l].Weight = AnimLayerWeight(rig, l, selection);
        compose[l].Mode = bound.Mode;
        compose[l].Mask = bound.Mask;
        if (bound.Mode == AnimLayerMode::Additive)
        {
            SampleAnimPlayback(sources, layer.Playing, tick, tickSeconds, true, scratch, scratch.References[l]);
            compose[l].Reference = scratch.References[l];
        }
    }
    ComposeAnimPose(*sources.Skeleton, std::span(compose.data(), layers), out);
}
