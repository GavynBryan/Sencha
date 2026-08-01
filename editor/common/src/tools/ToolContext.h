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
struct EditorOverlayState;
struct ManipulationSink;
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

// The editor-wide authoring state a tool acts with: shared by several tools and
// by the panels beside them, which is what separates it from a setting a single
// tool owns as a member. These outlive any one document.
struct ToolAuthoringSettings
{
    GridSettings& Grid;
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
    EditorOverlayState& Overlay;
    // The brush-edit backend, for tools that preview/commit mesh edits (the edge cut).
    ManipulationSink& Sink;
    // The editor-wide texturing material: new brushes stamp it onto their
    // faces, Shift+RClick sampling writes it.
    ActiveMaterialState& ActiveMaterial;
    // Lets a tool hand off to another tool by id (e.g. the edge cut switches to
    // Select after committing). Set by the workspace once the registry exists.
    std::function<void(std::string_view)> ActivateTool;
};
