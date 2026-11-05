#include <graphics/vulkan/OutlineMaskTargets.h>

#include <graphics/vulkan/VulkanSamplerCache.h>

#include <array>

namespace
{
constexpr std::uint32_t kMaskQuietFrames = 120;
}

void OutlineStyleImage::Setup(const RendererServices& services)
{
    Services = services;
}

void OutlineStyleImage::Teardown()
{
    if (Bindless.IsValid())
        Services.Descriptors->UnregisterSampledImage(Bindless);
    if (!Image.IsNull())
        Services.Images->Destroy(Image);
    Bindless = {};
    Image = {};
    UploadedVersion = 0;
}

std::uint32_t OutlineStyleImage::Update(const OutlineStyleTable& styles, std::uint32_t version)
{
    if (Image.IsNull())
    {
        ImageCreateInfo info;
        info.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
        info.Extent = { static_cast<std::uint32_t>(styles.size()) * 2, 1 };
        info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.DebugName = "outline_styles";
        Image = Services.Images->Create(info);
        if (Image.IsNull())
            return UINT32_MAX;
        UploadedVersion = 0;
    }
    if (UploadedVersion != version)
    {
        // Two texels per slot, read by texelFetch in output_present.frag.glsl:
        // colour and width, then priority.
        std::array<float, (kMaxOutlineStyles + 1) * 8> texels{};
        for (std::size_t slot = 0; slot < styles.size(); ++slot)
        {
            const OutlineStyleEntry& style = styles[slot];
            texels[slot * 8 + 0] = style.LinearColor.X;
            texels[slot * 8 + 1] = style.LinearColor.Y;
            texels[slot * 8 + 2] = style.LinearColor.Z;
            texels[slot * 8 + 3] = style.WidthPixels;
            texels[slot * 8 + 4] = static_cast<float>(style.Priority);
        }
        if (!Services.Images->Upload(Image, texels.data(), sizeof(texels)))
            return UINT32_MAX;
        UploadedVersion = version;
        if (!Bindless.IsValid())
            Bindless = Services.Descriptors->RegisterSampledImage(Image, Services.Samplers->GetNearestClamp());
    }
    return Bindless.IsValid() ? Bindless.Value : UINT32_MAX;
}

void OutlineMaskTargets::Setup(const RendererServices& services)
{
    Targets.Setup(services);
    RenderTargetDesc mask{};
    mask.ColorFormat = kOutlineMaskFormat;
    mask.Read = RenderTargetRead::Bindless;
    mask.Sampler = services.Samplers->GetNearestClamp();
    mask.DebugName = "outline_mask";
    mask.Lifetime = RenderTargetLifetime::WithinFrame;
    Mask = Targets.Create(mask);
    StyleImage.Setup(services);
}

void OutlineMaskTargets::Teardown()
{
    StyleImage.Teardown();
    Targets.Teardown();
}

void OutlineMaskTargets::NoteQuietFrame()
{
    if (++QuietFrames == kMaskQuietFrames)
        Targets.Evict(Mask);
}

std::optional<OutlineMaskAttachments> OutlineMaskTargets::Begin(const FrameContext& frame, const OutlineFrame& outlines)
{
    QuietFrames = 0;
    if (frame.ScenePost == nullptr || frame.SceneColorBindless == UINT32_MAX || frame.DepthView == VK_NULL_HANDLE)
        return std::nullopt;

    Targets.SetExtent(Mask, frame.SceneExtent);
    const std::optional<RenderTargetView> mask = Targets.Acquire(Mask);
    StyleBindless = StyleImage.Update(outlines.Styles, outlines.StylesVersion);
    if (!mask.has_value() || StyleBindless == UINT32_MAX)
        return std::nullopt;

    MaskBindless = mask->BindlessIndex;
    return OutlineMaskAttachments{ .MaskImage = mask->ColorImage,
                                   .MaskView = mask->ColorView,
                                   .MaskAccess = mask->ColorAccess,
                                   .Extent = mask->Extent,
                                   .DepthView = frame.DepthView,
                                   .DepthFormat = frame.DepthFormat,
                                   .StencilFormat = frame.StencilFormat };
}

void OutlineMaskTargets::Publish(const FrameContext& frame, const OutlineFrame& outlines)
{
    frame.ScenePost->OutlineMaskBindless = MaskBindless;
    frame.ScenePost->OutlineStyleBindless = StyleBindless;
    frame.ScenePost->OutlineMaxWidthPixels = outlines.MaxWidthPixels;
}

bool OutlineMaskTargets::HoldsMaskImage()
{
    return Targets.Peek(Mask).has_value();
}
