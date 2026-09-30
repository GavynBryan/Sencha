#include "ViewportLayout.h"

#include <algorithm>

ViewportLayout ViewportLayout::MakeDefault()
{
    ViewportLayout layout;
    const ViewportId perspective = layout.Add(ViewportOrientation::Perspective);
    layout.Add(ViewportOrientation::Top);
    layout.Add(ViewportOrientation::Front);
    layout.Add(ViewportOrientation::Right);
    layout.SetActive(perspective);
    return layout;
}

EditorViewport* ViewportLayout::Active()
{
    return Find(ActiveId);
}

const EditorViewport* ViewportLayout::Active() const
{
    return Find(ActiveId);
}

EditorViewport* ViewportLayout::Find(ViewportId id)
{
    const auto it = std::find_if(Viewports.begin(), Viewports.end(),
                                 [id](const ViewportStorage& viewport)
                                 {
                                     return viewport != nullptr && viewport->Id == id;
                                 });
    return it != Viewports.end() ? it->get() : nullptr;
}

const EditorViewport* ViewportLayout::Find(ViewportId id) const
{
    const auto it = std::find_if(Viewports.begin(), Viewports.end(),
                                 [id](const ViewportStorage& viewport)
                                 {
                                     return viewport != nullptr && viewport->Id == id;
                                 });
    return it != Viewports.end() ? it->get() : nullptr;
}

std::span<ViewportLayout::ViewportStorage> ViewportLayout::All()
{
    return Viewports;
}

std::span<const ViewportLayout::ViewportStorage> ViewportLayout::All() const
{
    return Viewports;
}

ViewportId ViewportLayout::Add(ViewportOrientation orientation)
{
    auto viewport = std::make_unique<EditorViewport>();
    viewport->Id = NextId;
    ++NextId.Value;
    viewport->ApplyOrientation(orientation);

    const ViewportId id = viewport->Id;
    Viewports.push_back(std::move(viewport));
    if (!ActiveId.IsValid())
        ActiveId = id;
    SyncActiveFlags();
    return id;
}

void ViewportLayout::SetActive(ViewportId id)
{
    ActiveId = Find(id) != nullptr ? id : ViewportId{};
    SyncActiveFlags();
}

void ViewportLayout::OnResize(uint32_t width, uint32_t height)
{
    Width = width;
    Height = height;

    // Seed any not-yet-arranged viewport with a full-window rect. The arrangement
    // writes the real rects from the views panel's rect, which is known only
    // after the first frame's input -- so without this the first frame would
    // project / aspect-test against a degenerate {0,0} box. Only fills
    // uninitialized rects, so it never clobbers the live rects already on
    // screen when a later resize arrives.
    for (const ViewportStorage& viewport : Viewports)
    {
        if (viewport == nullptr)
            continue;
        if (viewport->RegionMax.X <= viewport->RegionMin.X
            || viewport->RegionMax.Y <= viewport->RegionMin.Y)
        {
            viewport->RegionMin = Vec2d(0.0f, 0.0f);
            viewport->RegionMax = Vec2d(static_cast<float>(width), static_cast<float>(height));
        }
    }
}

ViewportId ViewportLayout::ResolveAt(Vec2d point) const
{
    for (const ViewportStorage& viewport : Viewports)
    {
        if (viewport != nullptr && viewport->Contains(point))
            return viewport->Id;
    }
    return ViewportId{};
}

void ViewportLayout::SyncActiveFlags()
{
    for (const ViewportStorage& viewport : Viewports)
    {
        if (viewport != nullptr)
            viewport->IsActive = viewport->Id == ActiveId;
    }
}
