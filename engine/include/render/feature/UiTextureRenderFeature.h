#pragma once

#include <graphics/RenderFeature.h>
#include <graphics/vulkan/UiTextureTargets.h>
#include <render/TargetTextures.h>

#include <string_view>

class TextureCache;
class UiService;

//=============================================================================
// UiTextureRenderFeature
//
// Draws every Texture-destined surface into its target texture, which materials
// sample as `target:NAME` (render/TargetTextures.h): authored UI on a screen in
// the world. The engine stages it for every host with a UI and a device, so a
// game names a texture on a surface and a material, and writes no render code.
// The recording, and when it is skipped, is UiTextureTargets'.
//=============================================================================

inline constexpr std::string_view kUiTextureRenderFeatureId = "ui_textures";

class UiTextureRenderFeature final : public IRenderFeature
{
public:
    // Without `textures` content images do not draw, as the runtime does not
    // resolve them.
    UiTextureRenderFeature(UiService& ui, TextureCache* textures, TargetTextureTable& targets);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Offscreen; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

private:
    UiService& Ui;
    TextureCache* Textures = nullptr;
    TargetTextureTable& Table;
    UiTextureTargets Targets;
    bool Ready = false;
};
