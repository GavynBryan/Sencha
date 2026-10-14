#include "ui/LevelViewSurface.h"

#include "icons/IconNames.h"
#include "render/EditorRenderFeature.h"
#include "viewport/ViewportLayout.h"

#include "ui/DocumentLengths.h"
#include "ui/EditorThemeFile.h"

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

} // namespace

LevelViewSurface::LevelViewSurface(UiService& ui, ViewportLayout& layout, const EditorOverlayState& overlay,
                                   const MarqueeState& marquee, EditorRenderFeature* render)
    : Ui(ui)
    , Layout(layout)
    , Overlay(overlay)
    , Marquee(marquee)
    , Render(render)
    , Screen(ui, Describe)
{
    for (std::size_t slot = 0; slot < kSlots; ++slot)
        Images[slot] = Ui.SetHostImage(SlotName(slot), RenderExtent{ 1, 1 });
}

LevelViewSurface::~LevelViewSurface()
{
    Screen.Close();
    if (Render != nullptr)
        Render->SetViewImages({});
    for (std::size_t slot = 0; slot < kSlots; ++slot)
        Ui.RemoveHostImage(SlotName(slot));
}

void LevelViewSurface::MoveTo(UiSurfaceId content)
{
    // Shown before it had a surface to open on, it opens on this one.
    Screen.MoveTo(content);
    if (Shown)
        (void)Screen.Open();
}

void LevelViewSurface::SetShown(bool shown)
{
    Shown = shown;
    if (shown)
        (void)Screen.Open();
    else
        Screen.Close();
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
        row.Parts = { PixelLength(tile.Position.X), PixelLength(tile.Position.Y), PixelLength(tile.Width()), PixelLength(tile.Height()) };
        rows.push_back(std::move(row));
    }
    if (Render != nullptr)
        Render->SetViewImages(std::move(images));
    if (Screen.IsOpen())
        (void)Ui.SetRows(Screen.Handle(), kTiles, rows);
    ShowMarks(scale);
}

void LevelViewSurface::ShowMarks(float scale)
{
    if (!Screen.IsOpen())
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
        UiRow row{ mark.Text, std::string(ActionToneName(mark.Tone)), KindName(mark.Kind) };
        row.Icon = std::string(IconName(mark.Icon));
        row.Selected = mark.Hot;
        row.Disabled = !mark.Enabled;
        row.Parts = { PixelLength(mark.Rect.Position.X), PixelLength(mark.Rect.Position.Y), PixelLength(mark.Rect.Width()),
                      PixelLength(mark.Rect.Height()), ThemeColorCss(mark.Color), ThemeColorCss(mark.Border) };
        rows.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen.Handle(), kMarks, rows);
}
