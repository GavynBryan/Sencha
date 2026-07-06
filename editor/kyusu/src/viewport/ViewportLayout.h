#pragma once

#include "EditorViewport.h"
#include "viewport/ViewportId.h"
#include "ViewportOrientation.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// The editor's viewport set: one perspective view and one switchable ortho
// view, each drawn by its own dock panel. This owns the viewports and the
// active-viewport state; on-screen placement belongs to the dock layout.
class ViewportLayout
{
public:
    using ViewportStorage = std::unique_ptr<EditorViewport>;

    static ViewportLayout MakeDefault();

    EditorViewport* Active();
    const EditorViewport* Active() const;
    EditorViewport* Find(ViewportId id);
    const EditorViewport* Find(ViewportId id) const;
    std::span<ViewportStorage> All();
    std::span<const ViewportStorage> All() const;
    ViewportId Add(ViewportOrientation orientation);
    void SetActive(ViewportId id);
    void OnResize(uint32_t width, uint32_t height);

    // The viewport whose on-screen rect contains `point` (window-space pixels), or
    // an invalid id if none. A hidden panel zeroes its viewport's rect, so only
    // views actually on screen can match. The one place a pixel maps to a
    // viewport; the input boundary stamps events with the result.
    [[nodiscard]] ViewportId ResolveAt(ImVec2 point) const;

private:
    void SyncActiveFlags();

    std::vector<ViewportStorage> Viewports;
    ViewportId ActiveId = {};
    ViewportId NextId = { 1 };
    uint32_t Width = 0;
    uint32_t Height = 0;
};
