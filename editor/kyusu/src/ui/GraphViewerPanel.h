#pragma once

#include "ui/IEditorPanel.h"

#include <math/Vec.h>
#include <zone/WorldPartitionIds.h>
#include <zone/ZoneId.h>

#include <string>

class CommandStack;
class SelectionService;
class ViewportLayout;
class WorldDocument;

class GraphViewerPanel final : public IEditorPanel
{
public:
    GraphViewerPanel(WorldDocument& world, SelectionService& selection,
                     CommandStack& commands, ViewportLayout& viewports);

    std::string_view GetTitle() const override { return "Graph Viewer"; }
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::CenterBottom; }
    int GetDockTabGroup() const override { return 0; }

private:
    WorldDocument& World;
    SelectionService& Selection;
    CommandStack& Commands;
    ViewportLayout& Viewports;
    float Yaw = 0.65f;
    float Pitch = -0.35f;
    float Zoom = 1.0f;
    Vec3d Center;
    bool ShowDocks = true;
    bool ShowLinks = true;
    bool CrossGraphOnly = false;
    bool OneWayOnly = false;
    bool PerspectiveProjection = true;
    int ValidationFilter = 0;
    GraphId FilterGraph;
    char Search[96] = {};
};
