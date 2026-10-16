#include <graphics/SwapchainImageLedger.h>

#include <algorithm>

void SwapchainImageLedger::Reset(std::uint32_t imageCount)
{
    Images.assign(imageCount, Entry{});
}

std::uint64_t SwapchainImageLedger::Acquired(std::uint32_t image)
{
    if (image >= Images.size())
        return 0;
    Entry& entry = Images[image];
    const std::uint64_t lastPresent = entry.Use == ImageUse::Presented ? entry.PresentId : 0;
    entry = Entry{ .Use = ImageUse::Held };
    return lastPresent;
}

void SwapchainImageLedger::Presented(std::uint32_t image, std::uint64_t presentId)
{
    if (image < Images.size())
        Images[image] = Entry{ .Use = ImageUse::Presented, .PresentId = presentId };
}

std::uint32_t SwapchainImageLedger::Count(ImageUse use) const
{
    return static_cast<std::uint32_t>(std::ranges::count(Images, use, &Entry::Use));
}
