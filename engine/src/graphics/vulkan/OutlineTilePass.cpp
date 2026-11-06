#include <graphics/vulkan/OutlineTilePass.h>

#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/RenderTargetSession.h>
#include <shaders/kOutlineTilesFragSpv.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
// Mirrored by the push block in outline_tiles.frag.glsl.
struct OutlineTilePush
{
    std::uint32_t SourceIndex = UINT32_MAX;
    std::int32_t ReachTiles = 0;
};

// `scissor` in tiles, grown by `grow` tiles each way and kept inside `tiles`.
VkRect2D TileRect(VkRect2D scissor, std::int32_t grow, VkExtent2D tiles)
{
    const auto size = static_cast<std::int32_t>(kOutlineTileSize);
    const std::int32_t left = std::max(scissor.offset.x / size - grow, 0);
    const std::int32_t top = std::max(scissor.offset.y / size - grow, 0);
    const std::int32_t right = std::min(
        (scissor.offset.x + static_cast<std::int32_t>(scissor.extent.width) + size - 1) / size + grow,
        static_cast<std::int32_t>(tiles.width));
    const std::int32_t bottom = std::min(
        (scissor.offset.y + static_cast<std::int32_t>(scissor.extent.height) + size - 1) / size + grow,
        static_cast<std::int32_t>(tiles.height));
    return VkRect2D{ { left, top },
                     { static_cast<std::uint32_t>(std::max(right - left, 0)),
                       static_cast<std::uint32_t>(std::max(bottom - top, 0)) } };
}
}

VkExtent2D OutlineTileExtent(VkExtent2D frame)
{
    return { (frame.width + kOutlineTileSize - 1) / kOutlineTileSize,
             (frame.height + kOutlineTileSize - 1) / kOutlineTileSize };
}

bool OutlineTilePass::Setup(const RendererServices& services)
{
    Shaders = services.Shaders;
    if (Shaders == nullptr || !Fullscreen.Setup(services, sizeof(OutlineTilePush)))
        return false;
    FragmentShader = Shaders->CreateModuleFromSpirv(kOutlineTilesFragSpv, kOutlineTilesFragSpvWordCount,
                                                    "Outline tiles fragment");
    return true;
}

void OutlineTilePass::Teardown()
{
    Pipeline.Reset();
    Fullscreen.Teardown();
    if (Shaders != nullptr)
    {
        Shaders->Destroy(FragmentShader);
        FragmentShader = {};
    }
    Shaders = nullptr;
}

std::uint32_t OutlineTilePass::Record(const FrameContext& frame, std::uint32_t maskBindless,
                                      const OutlineTileTargets& targets, VkRect2D scissor, float maxWidthPixels)
{
    const AttachmentFormatKey formats{ .Color = kOutlineTileFormat };
    const bool built = Pipeline.Ensure(formats, [&](std::size_t index) {
        const std::array grow{ ShaderSpecializationConstant{ .Id = 0, .Value = index == 1 ? 1u : 0u } };
        return Fullscreen.BuildPipeline(FragmentShader, formats, {}, grow);
    });
    if (!built)
        return UINT32_MAX;

    // The composite reaches past an object by the widest style, so a tile within
    // that many pixels of a marked one has work.
    const auto reach = static_cast<std::int32_t>(
        std::ceil((maxWidthPixels + 1.0f) / static_cast<float>(kOutlineTileSize)));
    const VkExtent2D tiles = targets.Marked.Extent;
    const auto pass = [&](const RenderTargetView& target, std::size_t variant, std::uint32_t source, VkRect2D area) {
        RenderTargetSession session(frame.Cmd, target.ColorImage, *target.ColorAccess);
        RenderScopeDesc desc{};
        // The whole image clears, so a tile outside what is drawn reads as empty.
        desc.Area.extent = tiles;
        desc.Color.View = target.ColorView;
        desc.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        desc.ColorFormat = kOutlineTileFormat;
        desc.Phase = RenderPhase::Post;
        const RenderScope scope(frame, desc);
        const OutlineTilePush push{ .SourceIndex = source, .ReachTiles = reach };
        Fullscreen.Draw(scope.Context().Cmd, tiles, area, Pipeline.Get(variant), &push);
    };
    // Growing reads `reach` tiles past the scissor, which marking must have covered.
    pass(targets.Marked, 0, maskBindless, TileRect(scissor, reach, tiles));
    pass(targets.Near, 1, targets.Marked.BindlessIndex, TileRect(scissor, 0, tiles));
    return targets.Near.BindlessIndex;
}
