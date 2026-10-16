#pragma once

#include <cstdint>
#include <vector>

// A swapchain's images as the application used them: held from acquire to
// present, then presented until acquired again. Only an acquire proves the
// display let one go, so presented images bound the presents it may still have.
class SwapchainImageLedger
{
public:
    void Reset(std::uint32_t imageCount);

    // The present `image` was last queued with; 0 if none, or it had no id.
    std::uint64_t Acquired(std::uint32_t image);
    void Presented(std::uint32_t image, std::uint64_t presentId);

    [[nodiscard]] std::uint32_t Held() const { return Count(ImageUse::Held); }
    [[nodiscard]] std::uint32_t PresentedNotReacquired() const { return Count(ImageUse::Presented); }

private:
    enum class ImageUse : std::uint8_t
    {
        Unused,
        Held,
        Presented,
    };

    struct Entry
    {
        ImageUse Use = ImageUse::Unused;
        std::uint64_t PresentId = 0;
    };

    [[nodiscard]] std::uint32_t Count(ImageUse use) const;

    std::vector<Entry> Images;
};
