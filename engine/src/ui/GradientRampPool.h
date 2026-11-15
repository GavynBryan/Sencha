#pragma once

#include <render/ui/UiDrawFrame.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class GradientRampPool
{
public:
    [[nodiscard]] UiGeneratedTextureId Acquire(std::vector<std::byte> pixels,
                                               UiGeneratedTextureId& nextGeneratedId,
                                               UiTextureChanges& changes);
    void Release(UiGeneratedTextureId ramp);
    void ReleaseUnused(UiTextureChanges& changes);

private:
    struct Ramp
    {
        std::string Pixels;
        std::uint32_t Users = 0;
    };

    std::unordered_map<UiGeneratedTextureId, Ramp> Ramps;
    // Keys view the Pixels of an entry in Ramps, whose nodes never move.
    std::unordered_map<std::string_view, UiGeneratedTextureId> RampByPixels;
    std::vector<UiGeneratedTextureId> Unused;
};
