#pragma once

#include <math/Vec.h>

#include <array>
#include <cstdint>

// A slot in the outline mask, 1 to 15; 0 is no outline. Docs: docs/renderer/outlines.md.
struct OutlineStyleId
{
    std::uint8_t Slot = 0;

    [[nodiscard]] bool IsValid() const { return Slot != 0; }
    friend bool operator==(OutlineStyleId, OutlineStyleId) = default;
};

inline constexpr std::size_t kMaxOutlineStyles = 15;

struct OutlineStyleDesc
{
    // sRGB, as a colour is authored; converted to linear once at registration.
    Vec<3> Color;
    float WidthPixels = 2.0f;
    // Where two styles compete for a pixel, the higher wins. Unique per World.
    std::int16_t Priority = 0;
};

// A style as the composite reads it.
struct OutlineStyleEntry
{
    Vec<3> LinearColor;
    float WidthPixels = 0.0f;
    std::int16_t Priority = 0;
};

using OutlineStyleTable = std::array<OutlineStyleEntry, kMaxOutlineStyles + 1>;

// What the outline composite needs from a frame's outlines, as plain data, so the
// backend that draws it never sees the queues behind them.
struct OutlineCompositeInputs
{
    const OutlineStyleTable* Styles = nullptr;
    // Moves when the styles change, so the table is uploaded only then.
    std::uint32_t StylesVersion = 0;
    float MaxWidthPixels = 0.0f;
    // What the outlined items cover in normalized device coordinates, x then y.
    float ScreenMin[2] = { 1.0f, 1.0f };
    float ScreenMax[2] = { -1.0f, -1.0f };
};
