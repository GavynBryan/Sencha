#include "EditorRenderFeature.h"

#include "PreviewBuffer.h"

#include "EditorTheme.h"
#include "viewport/ViewportLayout.h"
#include "viewport/ViewportShading.h"

#include <core/assets/RuntimeAssets.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleTypes.h>

#include <graphics/vulkan/VulkanBarriers.h>

#include <optional>
#include <variant>
#include <vector>

EditorRenderFeature::EditorRenderFeature(ViewportLayout& viewportLayout,
                                         EditorScene& scene,
                                         SelectionService& selection,
                                         MeshEditService& meshEdit,
                                         const EditorOverlayState& overlay,
                                         PreviewBuffer& preview,
                                         ManipulatorSession& session,
                                         const GridSettings& grid,
                                         LoggingProvider& logging,
                                         const ConsoleRegistry& console,
                                         AssetSystem* assets,
                                         const AssetRegistry* catalog,
                                         RuntimeAssets* runtimeAssets,
                                         const EditorDocument& document)
    : Layout(viewportLayout)
    , GridCfg(grid)
    , BrushSolid(scene, Solid)
    , Meshes(scene, Solid, logging, assets, catalog)
    , Wireframe(scene, selection, overlay, Lines)
    , Visuals(scene, Lines)
    , Highlight(scene, selection, meshEdit, overlay, session, WideLines, Fills)
    , Preview(preview, Lines)
    , Console(&console)
{
    BodyRenderers[static_cast<std::size_t>(ViewportShading::Wireframe)] = &Wireframe;

    // Real-material WYSIWYG path when an asset environment is present (the editor
    // always has one). The builder + SceneSolidRenderer replace BrushSolid for the
    // Solid body, and the placed-mesh queue replaces the StaticMeshRenderer draw.
    // The IBrushBodyRenderer seam stays at two implementations (Wireframe + SceneSolid).
    if (runtimeAssets != nullptr)
    {
        MeshCache = &runtimeAssets->StaticMeshes;
        MaterialStore = &runtimeAssets->Materials;
        QueueBuilder.emplace(document, runtimeAssets->Assets, runtimeAssets->StaticMeshes,
                             runtimeAssets->MaterialSets, logging);
        SceneSolid.emplace(Forward, *QueueBuilder, runtimeAssets->StaticMeshes,
                           runtimeAssets->Materials);
        MaterialPath = true;
        BodyRenderers[static_cast<std::size_t>(ViewportShading::Solid)] = &*SceneSolid;
    }
    else
    {
        // No asset environment (brush-only/headless): fall back to the procedural checker.
        BodyRenderers[static_cast<std::size_t>(ViewportShading::Solid)] = &BrushSolid;
    }
}

void EditorRenderFeature::Setup(const RendererServices& services)
{
    Services = services;
    Log = services.Logging ? &services.Logging->GetLogger<EditorRenderFeature>() : nullptr;
    Backdrop.Setup(services);
    Grid.Setup(services);
    Solid.Setup(services);
    Forward.Setup(services);
    Lines.Setup(services);
    WideLines.Setup(services);
    Fills.Setup(services);
    Targets.Setup(services);
    Bloom.Setup(services);
    if (Log != nullptr)
        Log->Info("EditorRenderFeature setup complete");
}

void EditorRenderFeature::OnDraw(const FrameContext& frame)
{
    if (!LoggedFirstDraw && Log != nullptr)
    {
        Log->Info("EditorRenderFeature drawing");
        LoggedFirstDraw = true;
    }

    // Match play-mode backface culling by default; the cvar lets you draw both sides to
    // diagnose inverted/missing-winding geometry. Missing cvar falls back to culling.
    bool cullBackfaces = true;
    if (const CVarMetadata* cvar = Console->FindCVar("editor.cull_backfaces");
        cvar != nullptr && std::holds_alternative<bool>(cvar->CurrentValue))
        cullBackfaces = std::get<bool>(cvar->CurrentValue);
    Solid.SetCullBackfaces(cullBackfaces);

    // Live look knobs via cvars (dial in the dev console).
    const auto readFloatCvar = [&](const char* name, float fallback)
    {
        if (const CVarMetadata* cvar = Console->FindCVar(name);
            cvar != nullptr && std::holds_alternative<double>(cvar->CurrentValue))
            return static_cast<float>(std::get<double>(cvar->CurrentValue));
        return fallback;
    };
    GridStyleCache.CellPx     = readFloatCvar("editor.grid.cell_px", 14.0f);
    GridStyleCache.Opacity    = readFloatCvar("editor.grid.opacity", 0.6f);
    GridStyleCache.Brightness = readFloatCvar("editor.grid.brightness", 0.62f);
    GridStyleCache.FadeStart  = readFloatCvar("editor.grid.fade_start", -0.3f);

    BloomEnabled = true;
    if (const CVarMetadata* cvar = Console->FindCVar("editor.bloom.enable");
        cvar != nullptr && std::holds_alternative<bool>(cvar->CurrentValue))
        BloomEnabled = std::get<bool>(cvar->CurrentValue);
    BloomParamsCache.Threshold = readFloatCvar("editor.bloom.threshold", 1.0f);
    BloomParamsCache.Intensity = readFloatCvar("editor.bloom.intensity", 1.0f);
    BloomParamsCache.Radius    = readFloatCvar("editor.bloom.radius", 2.0f);

    Targets.BeginFrame(frame.FrameInFlightIndex);

    // Build the scene draw queues once per frame; the per-viewport camera is applied at
    // draw time, so every viewport reuses the same brush + placed-mesh queues. Brush
    // geometry re-uploads only when the scene's brushes changed (dirty-tracked inside).
    if (MaterialPath)
    {
        QueueBuilder->Build();
        // Hemispheric ambient is live-tunable in the dev console, same path as the
        // grid/bloom knobs above. BuildLights() leaves the tints alone, so set them
        // after. Defaults match RenderLightSet's neutral cool fill.
        const float skyR    = readFloatCvar("render.ambient.sky_r", 0.10f);
        const float skyG    = readFloatCvar("render.ambient.sky_g", 0.12f);
        const float skyB    = readFloatCvar("render.ambient.sky_b", 0.15f);
        const float groundR = readFloatCvar("render.ambient.ground_r", 0.04f);
        const float groundG = readFloatCvar("render.ambient.ground_g", 0.03f);
        const float groundB = readFloatCvar("render.ambient.ground_b", 0.02f);
        RenderLightSet& lights = QueueBuilder->Lights();
        lights.AmbientSky    = Vec<3>(skyR, skyG, skyB);
        lights.AmbientGround = Vec<3>(groundR, groundG, groundB);
    }

    // Render only what the panel lays out: every leaf in quad mode, just the active
    // viewport in single mode (the others hold stale screen rects).
    std::vector<EditorViewport*> live;
    if (Layout.GetMode() == LayoutMode::Single)
    {
        if (EditorViewport* active = Layout.Active())
            live.push_back(active);
    }
    else
    {
        for (const auto& viewport : Layout.All())
            if (viewport != nullptr)
                live.push_back(&*viewport);
    }

    std::vector<ViewportId> liveIds;
    liveIds.reserve(live.size());
    for (EditorViewport* viewport : live)
    {
        liveIds.push_back(viewport->Id);
        if (std::optional<ViewportTargetCache::RenderView> target = Targets.AcquireForRender(viewport->Id))
            RenderViewportOffscreen(frame, *viewport, *target);
    }

    // Drop targets for viewports the layout no longer shows.
    Targets.Prune(liveIds);
}

void EditorRenderFeature::RenderViewportOffscreen(const FrameContext& frame, EditorViewport& viewport,
                                                  const ViewportTargetCache::RenderView& target)
{
    // The scene renderers derive their Vulkan viewport/scissor from the viewport's
    // screen rect; the offscreen target is origin-(0,0) and sized to the region, so
    // override the rect for this pass (same size, so the camera aspect is unchanged)
    // and restore it after. Picking reads the screen rect on the input path, not here.
    const ImVec2 savedMin = viewport.RegionMin;
    const ImVec2 savedMax = viewport.RegionMax;
    viewport.RegionMin = ImVec2(0.0f, 0.0f);
    viewport.RegionMax = ImVec2(static_cast<float>(target.Extent.width),
                                static_cast<float>(target.Extent.height));

    const auto transitionColor = [&](VkImageLayout oldLayout, VkImageLayout newLayout,
                                     VkPipelineStageFlags2 srcStage, VkPipelineStageFlags2 dstStage,
                                     VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess)
    {
        VulkanBarriers::ImageTransition t{};
        t.Image = target.ColorImage;
        t.OldLayout = oldLayout;
        t.NewLayout = newLayout;
        t.SrcStage = srcStage;
        t.DstStage = dstStage;
        t.SrcAccess = srcAccess;
        t.DstAccess = dstAccess;
        t.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        VulkanBarriers::TransitionImage(frame.Cmd, t);
    };

    // Color: whatever it held (UNDEFINED on first use, else SHADER_READ from when it
    // was last sampled) -> COLOR_ATTACHMENT.
    transitionColor(*target.ColorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    // Depth is cleared and discarded each pass, so its prior contents never matter.
    {
        VulkanBarriers::ImageTransition t{};
        t.Image = target.DepthImage;
        t.OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        t.NewLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        t.SrcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        t.DstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        t.SrcAccess = 0;
        t.DstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                    | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        t.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        VulkanBarriers::TransitionImage(frame.Cmd, t);
    }

    VkRenderingAttachmentInfo colorAttach{};
    colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttach.imageView = target.ColorView;
    colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.clearValue.color = { { 0.05f, 0.09f, 0.12f, 1.0f } };

    VkRenderingAttachmentInfo depthAttach{};
    depthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttach.imageView = target.DepthView;
    depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttach.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea.offset = { 0, 0 };
    info.renderArea.extent = target.Extent;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colorAttach;
    info.pDepthAttachment = &depthAttach;

    vkCmdBeginRendering(frame.Cmd, &info);

    // Local frame context describing the offscreen target (RGBA16F linear); the scene
    // pipelines key on these formats and rebuild their RGBA16F variant transparently.
    FrameContext local{};
    local.Cmd = frame.Cmd;
    local.FrameInFlightIndex = frame.FrameInFlightIndex;
    local.TargetExtent = target.Extent;
    local.TargetFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    local.DepthView = target.DepthView;
    local.DepthFormat = Services.DepthFormat;
    local.Phase = RenderPhase::Offscreen;

    Backdrop.DrawViewport(local.Cmd, viewport, local.TargetExtent, local.TargetFormat, local.DepthFormat);
    Grid.DrawViewport(local.Cmd, viewport, GridCfg, GridStyleCache, local.TargetExtent, local.TargetFormat, local.DepthFormat);
    if (IBrushBodyRenderer* body = BodyRenderers[static_cast<std::size_t>(viewport.Shading)])
        body->DrawViewport(local, viewport);
    // Placed meshes draw in every viewport so they read regardless of shading: through
    // the real-material queue when active, else the procedural-checker fallback.
    if (MaterialPath)
        Forward.Draw(local, viewport.BuildRenderData(), QueueBuilder->Lights(),
                     QueueBuilder->MeshQueue(), *MeshCache, *MaterialStore);
    else
        Meshes.DrawViewport(local, viewport);
    Visuals.DrawViewport(local, viewport);
    Highlight.DrawViewport(local, viewport);
    Preview.DrawViewport(local, viewport);

    vkCmdEndRendering(frame.Cmd);

    // Color: COLOR_ATTACHMENT -> SHADER_READ_ONLY for the UI's ImGui::Image sample.
    transitionColor(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    *target.ColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (BloomEnabled)
        RecordViewportBloom(frame, viewport, target);

    viewport.RegionMin = savedMin;
    viewport.RegionMax = savedMax;
}

void EditorRenderFeature::RecordViewportBloom(const FrameContext& frame, EditorViewport& viewport,
                                              const ViewportTargetCache::RenderView& target)
{
    if (target.BloomImage[0] == VK_NULL_HANDLE)
        return;

    // Render the active wireframe ON TOP (no depth test) into the bloom source, so the
    // glow comes from the full line instead of the depth-tested scene copy (which clips
    // itself against its own near surface). The crisp visible wireframe still depth-tests
    // in the scene pass; this is purely the glow's source. The viewport depth is attached
    // but untested (the wide-line pipeline expects a depth format). Then bloom it onto the
    // scene; the color ends in SHADER_READ_ONLY for the UI composite.
    VulkanBarriers::ImageTransition toColor{};
    toColor.Image = target.BloomImage[0];
    toColor.OldLayout = *target.BloomLayout[0];
    toColor.NewLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.SrcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toColor.DstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColor.SrcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toColor.DstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColor.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VulkanBarriers::TransitionImage(frame.Cmd, toColor);

    const ImVec2 savedMin = viewport.RegionMin;
    const ImVec2 savedMax = viewport.RegionMax;
    viewport.RegionMin = ImVec2(0.0f, 0.0f);
    viewport.RegionMax = ImVec2(static_cast<float>(target.BloomExtent.width),
                                static_cast<float>(target.BloomExtent.height));

    VkRenderingAttachmentInfo glowColor{};
    glowColor.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    glowColor.imageView = target.BloomView[0];
    glowColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    glowColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    glowColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    glowColor.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

    VkRenderingAttachmentInfo glowDepth{};
    glowDepth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    glowDepth.imageView = target.DepthView;
    glowDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    glowDepth.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    glowDepth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    glowDepth.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo glowInfo{};
    glowInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    glowInfo.renderArea.extent = target.BloomExtent;
    glowInfo.layerCount = 1;
    glowInfo.colorAttachmentCount = 1;
    glowInfo.pColorAttachments = &glowColor;
    glowInfo.pDepthAttachment = &glowDepth;
    vkCmdBeginRendering(frame.Cmd, &glowInfo);

    FrameContext glowCtx{};
    glowCtx.Cmd = frame.Cmd;
    glowCtx.FrameInFlightIndex = frame.FrameInFlightIndex;
    glowCtx.TargetExtent = target.BloomExtent;
    glowCtx.TargetFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    glowCtx.DepthView = target.DepthView;
    glowCtx.DepthFormat = Services.DepthFormat;
    glowCtx.Phase = RenderPhase::Offscreen;
    Highlight.SubmitActiveGlowSource(glowCtx, viewport);

    vkCmdEndRendering(frame.Cmd);

    VulkanBarriers::ImageTransition toRead{};
    toRead.Image = target.BloomImage[0];
    toRead.OldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toRead.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.SrcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toRead.DstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toRead.SrcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toRead.DstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toRead.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VulkanBarriers::TransitionImage(frame.Cmd, toRead);
    *target.BloomLayout[0] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    Bloom.Record(frame, target, BloomParamsCache);

    viewport.RegionMin = savedMin;
    viewport.RegionMax = savedMax;
}

void EditorRenderFeature::ReleaseSceneResources()
{
    // Point the Solid body back at the checker so nothing dereferences the released
    // builder, then drop the brush GPU meshes + material refs while the caches live.
    BodyRenderers[static_cast<std::size_t>(ViewportShading::Solid)] = &BrushSolid;
    MaterialPath = false;
    SceneSolid.reset();
    QueueBuilder.reset();
    MeshCache = nullptr;
    MaterialStore = nullptr;
}

void EditorRenderFeature::Teardown()
{
    Backdrop.Teardown();
    Grid.Teardown();
    Solid.Teardown();
    Forward.Teardown();
    Lines.Teardown();
    WideLines.Teardown();
    Fills.Teardown();
    Targets.Teardown();
    Bloom.Teardown();
}
