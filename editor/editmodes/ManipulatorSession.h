#pragma once

#include "IEditSession.h"
#include "IManipulator.h"

#include <memory>
#include <vector>

struct ToolContext;
struct EditorViewport;
class SelectionService;
class MeshEditService;
struct ManipulationSink;
struct GridSettings;

// Generic edit session: owns the ordered manipulator list and routes a
// pointer-down to the first applicable manipulator that gets a hit, beginning its
// drag. Holds no scene knowledge — it reads selection/mode and previews/commits
// only through the injected SelectionService, MeshEditService, and
// ManipulationSink. Replaces the brush-coupled MeshEditSession. Adding a
// manipulator is a push_back here; nothing else changes. (08-select-tool-v2.md)
class ManipulatorSession : public IEditSession
{
public:
    ManipulatorSession(SelectionService& selection, MeshEditService& service, ManipulationSink& sink,
                       const GridSettings& grid);

    InputConsumed OnPointerDown(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;

    // Visuals for every applicable manipulator (drawn by the overlay renderer).
    void BuildVisuals(const EditorViewport& viewport, ManipulatorVisual& out) const;

private:
    SelectionService& Selection;
    MeshEditService& Service;
    ManipulationSink& Sink;
    const GridSettings& Grid;
    std::vector<std::unique_ptr<IManipulator>> Manipulators;
};
