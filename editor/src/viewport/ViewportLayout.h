#pragma once

#include "EditorViewport.h"
#include "ViewportId.h"
#include "ViewportOrientation.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

struct LayoutNode
{
    enum class NodeKind
    {
        Split,
        Leaf
    };

    enum class Axis
    {
        Horizontal,
        Vertical
    };

    NodeKind Kind = NodeKind::Leaf;
    Axis SplitAxis = Axis::Horizontal;
    float Ratio = 0.5f;
    std::unique_ptr<LayoutNode> First;
    std::unique_ptr<LayoutNode> Second;
    ViewportId Viewport = {};

    static std::unique_ptr<LayoutNode> MakeLeaf(ViewportId viewportId);
    static std::unique_ptr<LayoutNode> MakeSplit(Axis axis,
                                                 float ratio,
                                                 std::unique_ptr<LayoutNode> first,
                                                 std::unique_ptr<LayoutNode> second);
};

// How the viewport area is presented: the four-way split, or a single viewport
// whose orientation is switched by tabs.
enum class LayoutMode
{
    Quad,
    Single
};

class ViewportLayout
{
public:
    using ViewportStorage = std::unique_ptr<EditorViewport>;

    static ViewportLayout MakeFourWay();

    [[nodiscard]] LayoutMode GetMode() const { return Mode; }
    void SetMode(LayoutMode mode) { Mode = mode; }

    EditorViewport* Active();
    const EditorViewport* Active() const;
    EditorViewport* Find(ViewportId id);
    const EditorViewport* Find(ViewportId id) const;
    size_t IndexOf(const EditorViewport* viewport) const;
    std::span<ViewportStorage> All();
    std::span<const ViewportStorage> All() const;
    ViewportId Add(ViewportOrientation orientation);
    void Remove(ViewportId id);
    void SetActive(ViewportId id);
    LayoutNode& Tree();
    const LayoutNode& Tree() const;
    void OnResize(uint32_t width, uint32_t height);

    // The viewport whose on-screen rect contains `point` (window-space pixels), or
    // an invalid id if none. Single mode hit-tests only the visible (active)
    // viewport — the hidden ones keep stale quad rects. The one place a pixel maps
    // to a viewport; the input boundary stamps events with the result.
    [[nodiscard]] ViewportId ResolveAt(ImVec2 point) const;

private:
    void SyncActiveFlags();

    std::vector<ViewportStorage> Viewports;
    std::unique_ptr<LayoutNode> Root;
    LayoutMode Mode = LayoutMode::Quad;
    ViewportId ActiveId = {};
    ViewportId NextId = { 1 };
    uint32_t Width = 0;
    uint32_t Height = 0;
};
