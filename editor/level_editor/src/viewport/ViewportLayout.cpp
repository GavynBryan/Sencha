#include "ViewportLayout.h"

#include <algorithm>

ViewportLayout ViewportLayout::MakeDefault()
{
    ViewportLayout layout;
    const ViewportId perspective = layout.Add(ViewportOrientation::Perspective);
    layout.Add(ViewportOrientation::Top);
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

    // Seed any not-yet-drawn viewport with a full-window rect. ViewportPanel writes
    // the real per-panel rects during draw, which runs *after* the first frame's
    // input — so without this the first frame would project / aspect-test against a
    // degenerate {0,0} box. Only fills uninitialized rects, so it never clobbers the
    // live rects already on screen when a later resize arrives.
    for (const ViewportStorage& viewport : Viewports)
    {
        if (viewport == nullptr)
            continue;
        if (viewport->RegionMax.x <= viewport->RegionMin.x
            || viewport->RegionMax.y <= viewport->RegionMin.y)
        {
            viewport->RegionMin = ImVec2(0.0f, 0.0f);
            viewport->RegionMax = ImVec2(static_cast<float>(width), static_cast<float>(height));
        }
    }
}

ViewportId ViewportLayout::ResolveAt(ImVec2 point) const
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
