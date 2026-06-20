#include "ViewportLayout.h"

#include <algorithm>

std::unique_ptr<LayoutNode> LayoutNode::MakeLeaf(ViewportId viewportId)
{
    auto node = std::make_unique<LayoutNode>();
    node->Kind = NodeKind::Leaf;
    node->Viewport = viewportId;
    return node;
}

std::unique_ptr<LayoutNode> LayoutNode::MakeSplit(Axis axis,
                                                  float ratio,
                                                  std::unique_ptr<LayoutNode> first,
                                                  std::unique_ptr<LayoutNode> second)
{
    auto node = std::make_unique<LayoutNode>();
    node->Kind = NodeKind::Split;
    node->SplitAxis = axis;
    node->Ratio = ratio;
    node->First = std::move(first);
    node->Second = std::move(second);
    return node;
}

ViewportLayout ViewportLayout::MakeFourWay()
{
    ViewportLayout layout;
    const ViewportId perspective = layout.Add(ViewportOrientation::Perspective);
    const ViewportId top = layout.Add(ViewportOrientation::Top);
    const ViewportId front = layout.Add(ViewportOrientation::Front);
    const ViewportId left = layout.Add(ViewportOrientation::Left);

    layout.Root = LayoutNode::MakeSplit(
        LayoutNode::Axis::Vertical,
        0.5f,
        LayoutNode::MakeSplit(
            LayoutNode::Axis::Horizontal,
            0.5f,
            LayoutNode::MakeLeaf(perspective),
            LayoutNode::MakeLeaf(top)),
        LayoutNode::MakeSplit(
            LayoutNode::Axis::Horizontal,
            0.5f,
            LayoutNode::MakeLeaf(front),
            LayoutNode::MakeLeaf(left)));

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

size_t ViewportLayout::IndexOf(const EditorViewport* viewport) const
{
    const auto it = std::find_if(Viewports.begin(), Viewports.end(),
                                 [viewport](const ViewportStorage& candidate)
                                 {
                                     return candidate.get() == viewport;
                                 });
    if (it == Viewports.end())
        return Viewports.size();
    return static_cast<size_t>(std::distance(Viewports.begin(), it));
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

void ViewportLayout::Remove(ViewportId id)
{
    const auto it = std::remove_if(Viewports.begin(), Viewports.end(),
                                   [id](const ViewportStorage& viewport)
                                   {
                                       return viewport != nullptr && viewport->Id == id;
                                   });
    Viewports.erase(it, Viewports.end());

    if (ActiveId == id)
        ActiveId = Viewports.empty() ? ViewportId{} : Viewports.front()->Id;

    SyncActiveFlags();
}

void ViewportLayout::SetActive(ViewportId id)
{
    ActiveId = Find(id) != nullptr ? id : ViewportId{};
    SyncActiveFlags();
}

LayoutNode& ViewportLayout::Tree()
{
    return *Root;
}

const LayoutNode& ViewportLayout::Tree() const
{
    return *Root;
}

void ViewportLayout::OnResize(uint32_t width, uint32_t height)
{
    Width = width;
    Height = height;

    // Seed any not-yet-drawn viewport with a full-window rect. ViewportPanel writes
    // the real per-leaf rects during draw, which runs *after* the first frame's
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
    if (Mode == LayoutMode::Single)
    {
        const EditorViewport* active = Active();
        return (active != nullptr && active->Contains(point)) ? active->Id : ViewportId{};
    }

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
