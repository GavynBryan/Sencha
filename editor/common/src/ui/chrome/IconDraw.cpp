#include "IconDraw.h"

#include "fonts/IconsFontAwesome6.h"
#include "icons/IconNames.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <assets/cook/SvgRasterize.h>
#include <core/io/FileBytes.h>

namespace
{
using namespace EditorChrome;

#ifndef SENCHA_EDITOR_ICON_DIR
#define SENCHA_EDITOR_ICON_DIR "."
#endif

constexpr std::size_t kCount = static_cast<std::size_t>(IconId::Count);

// The sampling resolutions each icon is rasterized at, design pixels. A box is
// drawn from the smallest raster that covers it, so a 19 px box samples the
// 24 px raster and a 9 px box the 12 px one.
constexpr float kSizes[] = { 12.0f, 16.0f, 24.0f };
constexpr std::size_t kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);

// The Font Awesome glyph that stands in for each icon when its file cannot be
// baked. IconId::None has none and draws nothing.
constexpr std::array<const char*, kCount> kGlyphs = [] {
    std::array<const char*, kCount> t{};
    const auto row = [&](IconId id, const char* glyph) { t[static_cast<std::size_t>(id)] = glyph; };
    row(IconId::Pointer, ICON_FA_ARROW_POINTER);
    row(IconId::Move, ICON_FA_UP_DOWN_LEFT_RIGHT);
    row(IconId::Rotate, ICON_FA_ROTATE);
    row(IconId::Scale, ICON_FA_MAXIMIZE);
    row(IconId::Resize, ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER);
    row(IconId::Pivot, ICON_FA_CROSSHAIRS);
    row(IconId::Anchor, ICON_FA_ANCHOR);
    row(IconId::World, ICON_FA_GLOBE);
    row(IconId::Local, ICON_FA_LOCATION_ARROW);
    row(IconId::Box, ICON_FA_CUBE);
    row(IconId::Plane, ICON_FA_SQUARE);
    row(IconId::Cylinder, ICON_FA_DATABASE);
    row(IconId::Light, ICON_FA_LIGHTBULB);
    row(IconId::Camera, ICON_FA_VIDEO);
    row(IconId::Grid, ICON_FA_BORDER_ALL);
    row(IconId::GridFrame, ICON_FA_RULER_COMBINED);
    row(IconId::Snap, ICON_FA_MAGNET);
    row(IconId::ZoneBounds, ICON_FA_VECTOR_SQUARE);
    row(IconId::Play, ICON_FA_PLAY);
    row(IconId::Stop, ICON_FA_STOP);
    row(IconId::Hammer, ICON_FA_HAMMER);
    row(IconId::Cancel, ICON_FA_XMARK);
    row(IconId::Check, ICON_FA_CHECK);
    row(IconId::Folder, ICON_FA_FOLDER);
    row(IconId::Search, ICON_FA_MAGNIFYING_GLASS);
    row(IconId::Refresh, ICON_FA_ARROWS_ROTATE);
    row(IconId::ChevronDown, ICON_FA_CHEVRON_DOWN);
    row(IconId::Add, ICON_FA_PLUS);
    row(IconId::Delete, ICON_FA_TRASH);
    row(IconId::Eye, ICON_FA_EYE);
    row(IconId::EyeOff, ICON_FA_EYE_SLASH);
    row(IconId::Lock, ICON_FA_LOCK);
    row(IconId::Unlock, ICON_FA_LOCK_OPEN);
    row(IconId::Cut, ICON_FA_SCISSORS);
    row(IconId::Carve, ICON_FA_CROP_SIMPLE);
    row(IconId::Clip, ICON_FA_SLASH);
    row(IconId::ClipFront, ICON_FA_ARROW_RIGHT_TO_BRACKET);
    row(IconId::ClipBack, ICON_FA_ARROWS_LEFT_RIGHT_TO_LINE);
    row(IconId::ClipSplit, ICON_FA_OBJECT_UNGROUP);
    row(IconId::ClipCap, ICON_FA_SQUARE);
    row(IconId::ModeObject, ICON_FA_CUBE);
    row(IconId::ModeVertex, ICON_FA_CIRCLE_DOT);
    row(IconId::ModeEdge, ICON_FA_GRIP_LINES);
    row(IconId::ModeFace, ICON_FA_VECTOR_SQUARE);
    row(IconId::WindowMinimize, ICON_FA_WINDOW_MINIMIZE);
    row(IconId::WindowMaximize, ICON_FA_WINDOW_MAXIMIZE);
    row(IconId::WindowRestore, ICON_FA_WINDOW_RESTORE);
    row(IconId::WindowClose, ICON_FA_XMARK);
    return t;
}();

// Where an icon's rasters landed in the atlas. Px is 0 for a size that was
// not baked. The atlas pointer says which atlas the UVs belong to, so a
// context rebuilt after a bake (tests) never reads them.
struct Raster
{
    ImVec2 Uv0;
    ImVec2 Uv1;
    float Px = 0.0f;
};
std::array<std::array<Raster, kSizeCount>, kCount> g_Rasters{};
const ImFontAtlas* g_BakedAtlas = nullptr;

// The shell's mark: flat-white vector art like the icons, so it is baked as
// coverage and takes its color from a tint at draw time. Its height comes from
// the UI scale alone and never from a nameplate's dimensions -- letting a theme
// metric decide it would make a colour-and-spacing change rebuild the atlas.
constexpr float kLogoBakeHeight = 64.0f;
struct LogoRaster
{
    ImVec2 Uv0;
    ImVec2 Uv1;
    float Aspect = 0.0f;
};
LogoRaster g_Logo{};
}

namespace EditorChrome
{
ShellAtlasResult BakeAtlasArt(ImFontAtlas& atlas, const ShellAtlasKey& key)
{
    const float uiScale = key.UiScale;
    g_Rasters = {};
    g_Logo = {};
    g_BakedAtlas = nullptr;

    // Everything is rasterized before anything is packed: each rect has to be
    // reserved at its raster's size so one Build() places them all.
    std::optional<Image> logo;
    int logoRect = -1;
    if (!key.LogoPath.empty())
    {
        std::vector<std::byte> bytes;
        (void)ReadFileBytes(key.LogoPath, bytes);
        const auto logoH = static_cast<std::uint32_t>(std::max(1L, std::lround(kLogoBakeHeight * uiScale)));
        logo = RasterizeSvg(bytes, logoH);
        if (logo.has_value())
            logoRect = atlas.AddCustomRectRegular(static_cast<int>(logo->Width), static_cast<int>(logo->Height));
    }

    struct Rasterized
    {
        std::array<std::optional<Image>, kSizeCount> Size{};
        std::array<int, kSizeCount> Rect{};
    };
    std::array<Rasterized, kCount> rasters{};
    for (std::size_t i = 1; i < kCount; ++i)
    {
        const std::string_view name = IconName(static_cast<IconId>(i));
        if (name.empty())
            continue;
        std::vector<std::byte> bytes;
        if (!ReadFileBytes(std::string(SENCHA_EDITOR_ICON_DIR) + "/" + std::string(name) + ".svg", bytes))
            continue;
        for (std::size_t s = 0; s < kSizeCount; ++s)
        {
            const auto px = static_cast<std::uint32_t>(std::max(1L, std::lround(kSizes[s] * uiScale)));
            std::optional<Image> image = RasterizeSvg(bytes, px);
            if (!image.has_value())
                break;
            rasters[i].Rect[s] = atlas.AddCustomRectRegular(static_cast<int>(image->Width),
                                                            static_cast<int>(image->Height));
            rasters[i].Size[s] = std::move(image);
        }
    }

    // Build packs the rects and allocates the coverage plane the backend
    // uploads as white times alpha; the rasters' alpha is written straight in.
    atlas.Build();
    const auto blit = [&](const ImFontAtlasCustomRect& rect, const Image& image) {
        bool any = false;
        for (std::uint32_t y = 0; y < image.Height; ++y)
        {
            unsigned char* row = atlas.TexPixelsAlpha8 + (rect.Y + static_cast<int>(y)) * atlas.TexWidth + rect.X;
            for (std::uint32_t x = 0; x < image.Width; ++x)
            {
                const unsigned char alpha = image.Pixels[(static_cast<std::size_t>(y) * image.Width + x) * 4u + 3u];
                row[x] = alpha;
                any |= alpha != 0;
            }
        }
        return any;
    };

    int loaded = 0;
    for (std::size_t i = 1; i < kCount; ++i)
    {
        bool any = false;
        for (std::size_t s = 0; s < kSizeCount; ++s)
        {
            if (!rasters[i].Size[s].has_value())
                continue;
            const ImFontAtlasCustomRect* rect = atlas.GetCustomRectByIndex(rasters[i].Rect[s]);
            if (rect == nullptr || !rect->IsPacked() || atlas.TexPixelsAlpha8 == nullptr)
                continue;
            any |= blit(*rect, *rasters[i].Size[s]);
            Raster& raster = g_Rasters[i][s];
            atlas.CalcCustomRectUV(rect, &raster.Uv0, &raster.Uv1);
            raster.Px = static_cast<float>(rect->Width);
        }
        loaded += any ? 1 : 0;
    }

    bool logoBaked = false;
    if (logo.has_value())
    {
        const ImFontAtlasCustomRect* rect = atlas.GetCustomRectByIndex(logoRect);
        if (rect != nullptr && rect->IsPacked() && atlas.TexPixelsAlpha8 != nullptr)
        {
            logoBaked = blit(*rect, *logo);
            atlas.CalcCustomRectUV(rect, &g_Logo.Uv0, &g_Logo.Uv1);
            g_Logo.Aspect = static_cast<float>(logo->Width) / static_cast<float>(logo->Height);
        }
    }

    g_BakedAtlas = &atlas;
    return ShellAtlasResult{ loaded, logoBaked };
}

float LogoAspect()
{
    return ImGui::GetIO().Fonts == g_BakedAtlas ? g_Logo.Aspect : 0.0f;
}

void DrawLogo(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (atlas != g_BakedAtlas || g_Logo.Aspect <= 0.0f || mx.x <= mn.x || mx.y <= mn.y)
        return;
    dl->AddImage(atlas->TexID, mn, mx, g_Logo.Uv0, g_Logo.Uv1, tint);
}

void DrawIcon(ImDrawList* dl, IconId id, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    const std::size_t index = static_cast<std::size_t>(id);
    if (index >= kCount)
        return;

    const float side = std::max(1.0f, std::min(mx.x - mn.x, mx.y - mn.y));
    const ImVec2 origin(std::floor(mn.x + (mx.x - mn.x - side) * 0.5f), std::floor(mn.y + (mx.y - mn.y - side) * 0.5f));

    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (atlas == g_BakedAtlas)
    {
        // The smallest raster that covers the box, else the largest baked.
        const Raster* pick = nullptr;
        for (const Raster& raster : g_Rasters[index])
        {
            if (raster.Px <= 0.0f)
                continue;
            pick = &raster;
            if (raster.Px >= side)
                break;
        }
        if (pick != nullptr)
        {
            dl->AddImage(atlas->TexID, origin, ImVec2(origin.x + side, origin.y + side), pick->Uv0, pick->Uv1, tint);
            return;
        }
    }

    const char* glyph = kGlyphs[index];
    if (glyph == nullptr)
        return;
    const ImVec2 size = ImGui::CalcTextSize(glyph);
    dl->AddText(ImVec2(std::floor(mn.x + (mx.x - mn.x - size.x) * 0.5f), std::floor(mn.y + (mx.y - mn.y - size.y) * 0.5f)),
                tint, glyph);
}
} // namespace EditorChrome
