#include "ui/LevelViewSurface.h"

#include "icons/IconNames.h"
#include "render/EditorRenderFeature.h"
#include "viewport/ViewportLayout.h"

#include <ui/UiService.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace
{
const UiModelRowsId kTiles = UiRowsIdAt(0);
const UiModelRowsId kMarks = UiRowsIdAt(1);

// Window pixels between neighbouring views.
constexpr float kGutter = 4.0f;

std::string SlotName(std::size_t slot)
{
    return std::format("level.view.{}", slot);
}

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://views.rml";
    desc.ModelName = "views";
    desc.RowLists = { "tiles", "marks" };
    return desc;
}

std::string Px(float value)
{
    return std::format("{}px", static_cast<int>(std::lround(value)));
}

// The overlay's colours are linear, as the scene's are; a document's are sRGB.
std::string CssColor(const Vec4& linear)
{
    const auto encode = [](float c) {
        c = std::clamp(c, 0.0f, 1.0f);
        const float srgb = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
        return static_cast<int>(std::lround(srgb * 255.0f));
    };
    return std::format("rgba({},{},{},{})", encode(linear.X), encode(linear.Y), encode(linear.Z),
                       static_cast<int>(std::lround(std::clamp(linear.W, 0.0f, 1.0f) * 255.0f)));
}

const char* KindName(ViewMarkKind kind)
{
    switch (kind)
    {
    case ViewMarkKind::Handle:  return "handle";
    case ViewMarkKind::Button:  return "button";
    case ViewMarkKind::Caption: return "caption";
    case ViewMarkKind::Tooltip: return "tooltip";
    case ViewMarkKind::Knob:    return "knob";
    case ViewMarkKind::Marquee: return "marquee";
    case ViewMarkKind::Marker:  return "marker";
    case ViewMarkKind::Label:
    default:                    return "label";
    }
}

const char* ToneName(ActionTone tone)
{
    switch (tone)
    {
    case ActionTone::Destructive: return "destructive";
    case ActionTone::Active:      return "active";
    case ActionTone::Normal:
    default:                      return "normal";
    }
}
} // namespace

LevelViewSurface::LevelViewSurface(UiService& ui, ViewportLayout& layout, const EditorOverlayState& overlay,
                                   const MarqueeState& marquee, EditorRenderFeature* render)
    : Ui(ui)
    , Layout(layout)
    , Overlay(overlay)
    , Marquee(marquee)
    , Render(render)
{
    for (std::size_t slot = 0; slot < kSlots; ++slot)
        Images[slot] = Ui.SetHostImage(SlotName(slot), RenderExtent{ 1, 1 });
}

LevelViewSurface::~LevelViewSurface()
{
    Close();
    if (Render != nullptr)
        Render->SetViewImages({});
    for (std::size_t slot = 0; slot < kSlots; ++slot)
        Ui.RemoveHostImage(SlotName(slot));
}

void LevelViewSurface::MoveTo(UiSurfaceId content)
{
    if (content == Surface)
        return;
    Close();
    Surface = content;
    if (Shown)
        Open();
}

void LevelViewSurface::SetShown(bool shown)
{
    Shown = shown;
    if (shown)
        Open();
    else
        Close();
}

void LevelViewSurface::Open()
{
    if (Screen.IsValid() || !Surface.IsValid())
        return;
    Screen = Ui.OpenScreen(Surface, Describe());
}

void LevelViewSurface::Close()
{
    if (!Screen.IsValid())
        return;
    Ui.CloseScreen(Screen);
    Screen = {};
}

void LevelViewSurface::Arrange(const Rect2d& content, ViewportArrangementKind kind, Vec2d divider, float scale)
{
    const bool showing = Shown && content.Area() > 0.0f;
    const std::vector<ViewportId> shown = showing ? ViewsShownIn(kind, Layout) : std::vector<ViewportId>{};
    const std::vector<Rect2d> tiles =
        shown.empty() ? std::vector<Rect2d>{} : ArrangeViewports(kind, content, divider, kGutter);

    std::vector<EditorRenderFeature::ViewImage> images;
    std::vector<UiRow> rows;
    for (const ViewportLayout::ViewportStorage& viewport : Layout.All())
    {
        if (viewport == nullptr)
            continue;
        const auto at = std::ranges::find(shown, viewport->Id);
        if (at == shown.end())
        {
            // Not on screen: no rect, so nothing routes input to it.
            viewport->RegionMin = {};
            viewport->RegionMax = {};
            continue;
        }
        const auto slot = static_cast<std::size_t>(at - shown.begin());
        if (slot >= tiles.size() || slot >= kSlots)
            continue;
        const Rect2d& tile = tiles[slot];
        viewport->RegionMin = tile.Min();
        viewport->RegionMax = tile.Max();
        if (Render != nullptr)
        {
            Render->GetViewportTargets().Show(viewport->Id, VkExtent2D{ static_cast<std::uint32_t>(tile.Width()),
                                                                         static_cast<std::uint32_t>(tile.Height()) });
            images.push_back({ .Viewport = viewport->Id, .Image = Images[slot] });
        }
        UiRow row{ viewport->GetDisplayLabel(), "", "" };
        row.Key = SlotName(slot);
        // Which view is active only means something while views share the content.
        row.Selected = viewport->IsActive && tiles.size() > 1;
        row.Parts = { Px(tile.Position.X), Px(tile.Position.Y), Px(tile.Width()), Px(tile.Height()) };
        rows.push_back(std::move(row));
    }
    if (Render != nullptr)
        Render->SetViewImages(std::move(images));
    if (Screen.IsValid())
        (void)Ui.SetRows(Screen, kTiles, rows);
    ShowMarks(scale);
}

void LevelViewSurface::ShowMarks(float scale)
{
    if (!Screen.IsValid())
        return;
    Marks.clear();
    for (const ViewportLayout::ViewportStorage& viewport : Layout.All())
        if (viewport != nullptr && viewport->RegionMax.X > viewport->RegionMin.X
            && viewport->RegionMax.Y > viewport->RegionMin.Y)
            AppendViewMarks(Overlay, Marquee, *viewport, scale, Marks);

    std::vector<UiRow> rows;
    rows.reserve(Marks.size());
    for (const ViewMark& mark : Marks)
    {
        UiRow row{ mark.Text, ToneName(mark.Tone), KindName(mark.Kind) };
        row.Icon = std::string(IconName(mark.Icon));
        row.Selected = mark.Hot;
        row.Disabled = !mark.Enabled;
        row.Parts = { Px(mark.Rect.Position.X), Px(mark.Rect.Position.Y), Px(mark.Rect.Width()),
                      Px(mark.Rect.Height()), CssColor(mark.Color), CssColor(mark.Border) };
        rows.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen, kMarks, rows);
}
