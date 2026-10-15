#pragma once

#include "render/EditorWideLinePipeline.h"
#include "render/StagePicture.h"
#include "viewport/OrbitCamera.h"

#include <math/geometry/3d/Aabb3d.h>
#include <render/ui/UiDrawFrame.h>

#include <vector>

// The Ability Workbench's stage: the preview drawn as lines (AbilityStageOf)
// into a picture the workspace shows, from a camera the author orbits.
class AbilityStageRenderFeature : public IRenderFeature
{
public:
    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Offscreen; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

    // Per frame it is seen: renders at `extent` into the host image named.
    void Show(VkExtent2D extent, UiHostImageId image) { Picture.Show(extent, image); }
    void SetStrokes(std::vector<EditorLineSegment> strokes) { Strokes = std::move(strokes); }
    void Orbit(float yaw, float pitch) { Camera.Orbit(yaw, pitch); }
    void Zoom(float delta) { Camera.Zoom(delta); }
    // Frames `bounds`; false when it holds nothing.
    bool Frame(const Aabb3d& bounds);

private:
    StagePicture Picture;
    EditorWideLinePipeline WideLines;
    std::vector<EditorLineSegment> Strokes;
    OrbitCamera Camera{ .Target = {}, .Yaw = 0.7f, .Pitch = 0.45f, .Distance = 8.0f };
};
