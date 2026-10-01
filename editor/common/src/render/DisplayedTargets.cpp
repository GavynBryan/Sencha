#include "render/DisplayedTargets.h"

void DisplayedTargets::Setup(const RendererServices& services)
{
    Store.Setup(services);
}

void DisplayedTargets::Teardown()
{
    Created.clear();
    Store.Teardown();
    Ledger = {};
}

RenderTargetId DisplayedTargets::Create(const RenderTargetDesc& desc)
{
    const RenderTargetId id = Store.Create(desc);
    Created.push_back(id);
    return id;
}

void DisplayedTargets::Destroy(RenderTargetId id)
{
    Store.Destroy(id);
    Ledger.Forget(id);
    std::erase(Created, id);
}

void DisplayedTargets::BeginFrame(std::uint32_t frameInFlightIndex)
{
    Store.BeginFrame(frameInFlightIndex);
    Ledger.Advance();
    for (const RenderTargetId id : Created)
    {
        if (!Ledger.WasDisplayed(id))
            Store.Evict(id);
    }
}

std::optional<RenderTargetView> DisplayedTargets::Acquire(RenderTargetId id)
{
    if (!Ledger.WasDisplayed(id))
        return std::nullopt;
    return Store.Acquire(id);
}

void DisplayedTargets::Show(RenderTargetId id, VkExtent2D extent)
{
    Ledger.MarkDisplayed(id);
    Store.SetExtent(id, extent);
}
