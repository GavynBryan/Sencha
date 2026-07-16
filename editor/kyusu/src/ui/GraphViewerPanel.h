#pragma once

#include "ui/IEditorPanel.h"

#include <math/Vec.h>
#include <zone/WorldPartitionIds.h>
#include <zone/ZoneId.h>

#include <string>
#include <vector>

class CommandStack;
class SelectionService;
class WorldDocument;

class GraphViewerPanel final : public IEditorPanel
{
public:
    GraphViewerPanel(WorldDocument& world, SelectionService& selection,
                     CommandStack& commands);

    std::string_view GetTitle() const override { return "Graph Viewer"; }
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::CenterBottom; }
    int GetDockTabGroup() const override { return 0; }

private:
    WorldDocument& World;
    SelectionService& Selection;
    CommandStack& Commands;
    float Yaw = 0.65f;
    float Pitch = -0.35f;
    float Zoom = 1.0f;
    Vec3d Center;
    bool ShowDocks = true;
    bool ShowLinks = true;
    bool CrossGraphOnly = false;
    bool OneWayOnly = false;
    bool PreviewResidency = false;
    bool PerspectiveProjection = true;
    int ResidencyFilter = 0;
    int ValidationFilter = 0;
    GraphId FilterGraph;
    char Search[96] = {};
    std::vector<ZoneId> SelectedNodes;
};
