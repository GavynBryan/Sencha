#include "render/DisplayedTargets.h"

void DisplayedTargets::Setup(const RendererServices& services, VkSampler sampler)
{
    Store.Setup(services);
    Presenter.Setup(sampler);
}

void DisplayedTargets::Teardown()
{
    for (const RenderTargetId id : Created)
        Presenter.Release(id);
    Created.clear();
    // The store waits the device out, which is also what makes freeing the
    // presenter's sets safe.
    Store.Teardown();
    Presenter.Teardown();
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
    Presenter.Release(id);
    Store.Destroy(id);
    Ledger.Forget(id);
    std::erase(Created, id);
}

void DisplayedTargets::BeginFrame(std::uint32_t frameInFlightIndex, GpuFrameRetirement retirement)
{
    Store.BeginFrame(frameInFlightIndex);
    Presenter.BeginFrame(retirement);
    Ledger.Advance();
    for (const RenderTargetId id : Created)
    {
        if (Ledger.WasDisplayed(id))
            continue;
        // The set goes first: it names the views the eviction retires.
        Presenter.Release(id);
        Store.Evict(id);
    }
}

std::optional<RenderTargetView> DisplayedTargets::Acquire(RenderTargetId id)
{
    if (!Ledger.WasDisplayed(id))
        return std::nullopt;
    return Store.Acquire(id);
}

ImTextureID DisplayedTargets::Display(RenderTargetId id, VkExtent2D extent)
{
    Ledger.MarkDisplayed(id);
    Store.SetExtent(id, extent);
    return Presenter.Present(Store, id);
}
