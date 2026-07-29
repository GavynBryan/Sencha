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
struct ActiveMaterialState;

// The transient editing state a tool reads and writes: the live interaction,
// the geometry it previews, the marquee, and the viewport overlay. They are
// created, reset, and torn down together with the document being edited, so
// they reach a tool as one group rather than four arguments.
struct ToolInteractionState
{
    InteractionHost& Interactions;
    PreviewBuffer& Preview;
    MarqueeState& Marquee;
    EditorOverlayState& Overlay;
};

// The authoring settings a tool acts with. These outlive any one document (they
// are editor-wide and toolbar-surfaced), which is what separates them from the
// interaction state above.
struct ToolAuthoringSettings
{
    GridSettings& Grid;
    BrushCreationSettings& BrushCreate;
    EdgeCutSettings& EdgeCut;
    ActiveMaterialState& ActiveMaterial;
};

struct ToolContext
{
    ToolContext(CommandStack& commandStack,
                SelectionService& selectionService,
                PickingService& pickingService,
                EditorScene& levelScene,
                EditorDocument& levelDocument,
                MeshEditService& meshEdit,
                ManipulationSink& sink,
                ToolInteractionState interaction,
                ToolAuthoringSettings settings);

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
    // The editor-wide texturing material: new brushes stamp it onto their
    // faces, Shift+RClick sampling writes it.
    ActiveMaterialState& ActiveMaterial;
    // Lets a tool hand off to another tool by id (e.g. the edge cut switches to
    // Select after committing). Set by the workspace once the registry exists.
    std::function<void(std::string_view)> ActivateTool;
};
