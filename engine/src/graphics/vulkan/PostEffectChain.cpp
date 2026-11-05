#include <graphics/vulkan/PostEffectChain.h>

#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/RenderTargetSession.h>
#include <graphics/vulkan/VulkanSamplerCache.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

namespace
{
// Frames of nothing active before the planes go, so an effect that pulses does
// not reallocate them each time.
constexpr std::uint32_t kPlaneQuietFrames = 120;

// Mirrored by the push block in post_effect.glsli.
struct PostEffectPush
{
    std::uint32_t SceneIndex = UINT32_MAX;
    std::uint32_t DepthIndex = UINT32_MAX;
    std::uint32_t MaskIndex = UINT32_MAX;
    std::uint32_t Pad = 0;
    float TexelSize[2]{};
    float WallSeconds = 0.0f;
    float Weight = 0.0f;
    std::array<float, kPostEffectParamCount> Params{};
};
static_assert(sizeof(PostEffectPush) == 96);
// The guaranteed minimum push-constant range.
static_assert(sizeof(PostEffectPush) <= 128);
}

bool PostEffectChain::Setup(const RendererServices& services, const PostEffectCatalog& catalog)
{
    Catalog = &catalog;
    Shaders = services.Shaders;
    if (Shaders == nullptr || !Fullscreen.Setup(services, sizeof(PostEffectPush)) || !Transform.Setup(services))
        return false;

    Planes.Setup(services);
    RenderTargetDesc plane{};
    plane.ColorFormat = kSceneColorFormat;
    plane.Read = RenderTargetRead::Bindless;
    plane.Sampler = services.Samplers->GetLinearClamp();
    plane.DebugName = "post_effect_plane";
    plane.Lifetime = RenderTargetLifetime::WithinFrame;
    Plane[0] = Planes.Create(plane);
    Plane[1] = Planes.Create(plane);
    // Every effect compiles here rather than on the frame it first has weight,
    // which for a game's death or fade is the frame a hitch would be seen.
    for (std::uint32_t id = 1; id <= catalog.Count(); ++id)
        (void)Compile(PostEffectId{ id });
    return true;
}

void PostEffectChain::Teardown()
{
    Planes.Teardown();
    for (CompiledEffect& effect : Compiled)
        if (Shaders != nullptr)
            Shaders->Destroy(effect.Fragment);
    Compiled.clear();
    Transform.Teardown();
    Fullscreen.Teardown();
    Shaders = nullptr;
    Catalog = nullptr;
}

const PostEffectChain::CompiledEffect* PostEffectChain::Compile(PostEffectId effect)
{
    // A failed compile is kept too, so a broken shader is tried once rather than every frame.
    const auto known = std::ranges::find_if(Compiled, [&](const CompiledEffect& c) { return c.Effect == effect; });
    if (known != Compiled.end())
        return known->Pipeline != VK_NULL_HANDLE ? &*known : nullptr;

    const PostEffectType* type = Catalog != nullptr ? Catalog->Get(effect) : nullptr;
    if (type == nullptr)
        return nullptr;
    const ShaderHandle fragment = Shaders->CreateModuleFromSpirv(
        type->FragmentSpirv.data(), type->FragmentSpirv.size(), type->Name.c_str());
    const VkPipeline pipeline = Fullscreen.BuildPipeline(fragment, AttachmentFormatKey{ .Color = kSceneColorFormat });
    if (pipeline == VK_NULL_HANDLE)
    {
        Shaders->Destroy(fragment);
        Compiled.push_back({ .Effect = effect, .Fragment = {}, .Pipeline = VK_NULL_HANDLE });
        return nullptr;
    }
    Compiled.push_back({ .Effect = effect, .Fragment = fragment, .Pipeline = pipeline });
    return &Compiled.back();
}

std::size_t PostEffectChain::ResidentPlaneCount() const
{
    std::size_t resident = 0;
    for (const RenderTargetId id : Plane)
        resident += const_cast<RenderTargetStore&>(Planes).Peek(id).has_value() ? 1u : 0u;
    return resident;
}

void PostEffectChain::Record(const FrameContext& frame, const PostEffectFrame& effects,
                             const OutputTransformParams& output)
{
    if (effects.Entries.empty())
    {
        if (++QuietFrames == kPlaneQuietFrames)
            for (const RenderTargetId id : Plane)
                Planes.Evict(id);
        return;
    }
    QuietFrames = 0;
    if (frame.SceneColorBindless == UINT32_MAX || frame.ScenePost == nullptr)
        return;

    // A failed plane degrades to no effects rather than an illegal frame.
    Planes.BeginFrame(frame.FrameInFlightIndex);
    std::array<std::optional<RenderTargetView>, 2> planes;
    for (std::size_t i = 0; i < 2; ++i)
    {
        Planes.SetExtent(Plane[i], frame.SceneExtent);
        planes[i] = Planes.Acquire(Plane[i]);
        if (!planes[i].has_value())
            return;
    }

    std::uint32_t input = frame.SceneColorBindless;
    std::size_t target = 0;

    const auto intoPlane = [&](const auto& draw) {
        const RenderTargetView& plane = *planes[target];
        {
            RenderTargetSession session(frame.Cmd, plane.ColorImage, *plane.ColorAccess);
            RenderScopeDesc desc{};
            desc.Area.extent = plane.Extent;
            desc.Color.View = plane.ColorView;
            desc.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.ColorFormat = kSceneColorFormat;
            desc.Phase = RenderPhase::Post;
            const RenderScope scope(frame, desc);
            draw(scope.Context());
        }
        input = plane.BindlessIndex;
        target ^= 1u;
    };

    const auto runEffect = [&](const PostEffectFrameEntry& entry) {
        const CompiledEffect* compiled = Compile(entry.Effect);
        if (compiled == nullptr)
            return;
        intoPlane([&](const FrameContext& context) {
            PostEffectPush push{};
            push.SceneIndex = input;
            push.TexelSize[0] = 1.0f / static_cast<float>(frame.SceneExtent.width);
            push.TexelSize[1] = 1.0f / static_cast<float>(frame.SceneExtent.height);
            push.WallSeconds = effects.WallSeconds;
            push.Weight = entry.Weight;
            push.Params = entry.Params;
            Fullscreen.Draw(context.Cmd, context.TargetExtent, compiled->Pipeline, &push);
        });
    };

    for (const PostEffectFrameEntry& entry : effects.Entries)
        if (entry.Domain == PostEffectDomain::Scene)
            runEffect(entry);

    const bool anyDisplay = std::ranges::any_of(effects.Entries, [](const PostEffectFrameEntry& entry) {
        return entry.Domain == PostEffectDomain::Display;
    });
    if (anyDisplay)
    {
        intoPlane([&](const FrameContext& context) {
            FrameContext source = context;
            source.SceneColorBindless = input;
            source.ScenePost = nullptr;
            Transform.Draw(source, output);
        });
        frame.ScenePost->TransformApplied = true;
        for (const PostEffectFrameEntry& entry : effects.Entries)
            if (entry.Domain == PostEffectDomain::Display)
                runEffect(entry);
    }
    frame.ScenePost->PresentColorBindless = input;
}
