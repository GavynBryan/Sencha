#pragma once

#include <graphics/RenderFeature.h>
#include <graphics/vulkan/UiDrawPass.h>

#include <string_view>
#include <vector>

class TextureCache;
class UiService;

//=============================================================================
// UiRenderFeature
//
// Draws the authored UI recorded during extraction.
//
// Holds UiDrawPass by value -- the SkyGradientPass precedent. The pass takes
// plain render-domain data and names no render-domain type, so the closed
// recording set in render/pass/ does not grow a fourth member to accommodate
// authored UI (cmake/CheckRenderIsolation.cmake).
//
// It reads UiService::Frames() and nothing else: no document, no ECS, no editor
// selection, no game code. Everything it draws was decided during extraction.
//=============================================================================

// The id this feature stages under, so a host can order its own features
// against it.
inline constexpr std::string_view kUiRenderFeatureId = "application_ui";

class UiRenderFeature final : public IRenderFeature
{
public:
    // `textures` may be null, in which case content images do not draw -- the
    // same answer the runtime gives when asked to resolve one.
    UiRenderFeature(UiService& ui, TextureCache* textures);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::ApplicationUi; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

private:
    UiService& Ui;
    TextureCache* Textures = nullptr;
    UiDrawPass Pass;
    bool Ready = false;
};
