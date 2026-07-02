#pragma once

#include <functional>
#include <string_view>

class CommandStack;
class EditSessionHost;
class InteractionHost;
class EditorDocument;
class EditorScene;
class MeshEditService;
class PickingService;
class PreviewBuffer;
class SelectionService;
struct MarqueeState;
struct GridSettings;
struct BrushCreationSettings;
struct EditorOverlayState;
struct ManipulationSink;
struct EdgeCutSettings;

struct ToolContext
{
    ToolContext(CommandStack& commandStack,
                SelectionService& selectionService,
                PickingService& pickingService,
                EditorScene& levelScene,
                EditorDocument& levelDocument,
                InteractionHost& interactions,
                PreviewBuffer& preview,
                MeshEditService& meshEdit,
                MarqueeState& marquee,
                GridSettings& grid,
                BrushCreationSettings& brushCreate,
                EditorOverlayState& overlay,
                ManipulationSink& sink,
                EdgeCutSettings& edgeCut);

    CommandStack& Commands;
    SelectionService& Selection;
    PickingService& Picking;
    EditorScene& Scene;
    EditorDocument& Document;
    InteractionHost& Interactions;
    PreviewBuffer& Preview;
    MeshEditService& MeshEdit;
    MarqueeState& Marquee;
    GridSettings& Grid;
    BrushCreationSettings& BrushCreate;
    EditorOverlayState& Overlay;
    // The brush-edit backend, for tools that preview/commit mesh edits (the edge cut).
    ManipulationSink& Sink;
    EdgeCutSettings& EdgeCut;
    // Lets a tool hand off to another tool by id (e.g. the edge cut switches to
    // Select after committing). Set by the workspace once the registry exists.
    std::function<void(std::string_view)> ActivateTool;
};
