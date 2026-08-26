#include <string>
#include <graphics/vulkan/RenderTargetStore.h>

#include <graphics/vulkan/VulkanDeviceService.h>

#include <cassert>

void RenderTargetStore::Setup(const RendererServices& services)
{
    Services = services;
}

void RenderTargetStore::Teardown()
{
    // The image service defers destruction behind the retirement clock, which
    // no longer advances once features are tearing down; wait the device out so
    // the release below is immediate and complete.
    if (Services.Device != nullptr)
        vkDeviceWaitIdle(Services.Device->GetDevice());

    for (Target& target : Targets)
    {
        for (Slot& slot : target.Slots)
            ReleaseSlot(slot);
        target.Alive = false;
    }
    Targets.assign(1, Target{});
    FreeList.clear();
}

void RenderTargetStore::BeginFrame(std::uint32_t frameInFlightIndex)
{
    // GraphicsServices clamps the configured count, so the index is in range by
    // construction; the assert catches a broken contract in development and the
    // resolve keeps a release build from indexing past the array.
    assert(frameInFlightIndex < kMaxFramesInFlight);
    CurrentSlot = ResolveTargetSlot(frameInFlightIndex, kMaxFramesInFlight);
}

RenderTargetStore::Target* RenderTargetStore::Resolve(RenderTargetId id)
{
    if (!id.IsValid() || id.Index >= Targets.size())
        return nullptr;
    Target& target = Targets[id.Index];
    if (!target.Alive || target.Generation != id.Generation)
        return nullptr;
    return &target;
}

RenderTargetId RenderTargetStore::Create(const RenderTargetDesc& desc)
{
    std::uint32_t index;
    if (!FreeList.empty())
    {
        index = FreeList.back();
        FreeList.pop_back();
    }
    else
    {
        index = static_cast<std::uint32_t>(Targets.size());
        Targets.emplace_back();
    }

    Target& target = Targets[index];
    // Generation advances on reuse, so a handle held across a destroy cannot
    // address the slot's next occupant.
    ++target.Generation;
    target.Desc = desc;
    target.Alive = true;
    return RenderTargetId{ index, target.Generation };
}

void RenderTargetStore::Destroy(RenderTargetId id)
{
    Target* target = Resolve(id);
    if (target == nullptr)
        return;
    for (Slot& slot : target->Slots)
        ReleaseSlot(slot);
    target->Alive = false;
    target->Desc = RenderTargetDesc{};
    FreeList.push_back(id.Index);
}

void RenderTargetStore::SetExtent(RenderTargetId id, VkExtent2D extent)
{
    if (Target* target = Resolve(id))
        target->Desc.Extent = extent;
}

void RenderTargetStore::ReleaseSlot(Slot& slot)
{
    if (slot.Bindless.IsValid())
    {
        Services.Descriptors->UnregisterSampledImage(slot.Bindless);
        slot.Bindless = {};
    }
    if (!slot.Color.IsNull())
    {
        Services.Images->Destroy(slot.Color);
        slot.Color = {};
    }
    if (!slot.Depth.IsNull())
    {
        Services.Images->Destroy(slot.Depth);
        slot.Depth = {};
    }
    slot.ColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    slot.Built = RenderTargetDesc{};
}

void RenderTargetStore::BuildSlot(Slot& slot, const RenderTargetDesc& desc)
{
    // Destroy routes through the image service's deletion queue, so retiring
    // the previous images here is safe while an earlier frame still reads them.
    ReleaseSlot(slot);

    VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc.Read != RenderTargetRead::None)
        colorUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    ImageCreateInfo color;
    color.Format = desc.ColorFormat;
    color.Extent = desc.Extent;
    color.Usage = colorUsage;
    color.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    color.DebugName = desc.DebugName != nullptr ? desc.DebugName : "render_target_color";
    slot.Color = Services.Images->Create(color);
    if (slot.Color.IsNull())
        return;

    if (desc.DepthFormat != VK_FORMAT_UNDEFINED)
    {
        ImageCreateInfo depth;
        depth.Format = desc.DepthFormat;
        depth.Extent = desc.Extent;
        depth.Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depth.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        // Named after its target: every depth image sharing one label makes a
        // capture unreadable the moment a second viewport exists.
        const std::string depthName =
            std::string(desc.DebugName != nullptr ? desc.DebugName : "render_target")
            + "_depth";
        depth.DebugName = depthName.c_str();
        slot.Depth = Services.Images->Create(depth);
        if (slot.Depth.IsNull())
        {
            ReleaseSlot(slot);
            return;
        }
    }

    if (desc.Read == RenderTargetRead::Bindless && desc.Sampler != VK_NULL_HANDLE)
        slot.Bindless = Services.Descriptors->RegisterSampledImage(slot.Color, desc.Sampler);

    slot.ColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    slot.Built = desc;
}

std::optional<RenderTargetView> RenderTargetStore::Acquire(RenderTargetId id)
{
    Target* target = Resolve(id);
    if (target == nullptr)
        return std::nullopt;
    if (target->Desc.Extent.width == 0 || target->Desc.Extent.height == 0)
        return std::nullopt;

    Slot& slot = target->Slots[CurrentSlot];
    if (TargetSlotNeedsRebuild(slot.Built, target->Desc, !slot.Color.IsNull()))
        BuildSlot(slot, target->Desc);
    return Peek(id);
}

std::optional<RenderTargetView> RenderTargetStore::Peek(RenderTargetId id)
{
    Target* target = Resolve(id);
    if (target == nullptr)
        return std::nullopt;

    Slot& slot = target->Slots[CurrentSlot];
    if (slot.Color.IsNull())
        return std::nullopt;

    return RenderTargetView{
        .ColorImage = Services.Images->GetImage(slot.Color),
        .ColorView = Services.Images->GetView(slot.Color),
        .DepthImage = slot.Depth.IsNull() ? VK_NULL_HANDLE
                                          : Services.Images->GetImage(slot.Depth),
        .DepthView = slot.Depth.IsNull() ? VK_NULL_HANDLE
                                         : Services.Images->GetView(slot.Depth),
        .Extent = slot.Built.Extent,
        .ColorLayout = &slot.ColorLayout,
        .BindlessIndex = slot.Bindless.IsValid() ? slot.Bindless.Value : UINT32_MAX,
    };
}

std::size_t RenderTargetStore::ResidentCount() const
{
    std::size_t count = 0;
    for (const Target& target : Targets)
        count += target.Alive ? 1u : 0u;
    return count;
}
