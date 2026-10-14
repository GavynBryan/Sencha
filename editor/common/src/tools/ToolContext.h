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
class PlacementSession;
class PreviewBuffer;
class SelectionService;
struct MarqueeState;
struct GridSettings;
struct EditorOverlayState;
struct ManipulationSink;
class ActiveResources;

// The transient editing state a tool reads and writes: interaction, previews,
// pending placement, marquee, overlay. It is created, reset and torn down with
// the edited document, so it reaches a tool as one group.
struct ToolInteractionState
{
    InteractionHost& Interactions;
    PreviewBuffer& Preview;
    PlacementSession& Placement;
    MarqueeState& Marquee;
    EditorOverlayState& Overlay;
};

// The editor-wide authoring state a tool acts with: shared by several tools and
// by the panels beside them, which is what separates it from a setting a single
// tool owns as a member. These outlive any one document.
struct ToolAuthoringSettings
{
    GridSettings& Grid;
    ActiveResources& Resources;
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
    PlacementSession& Placement;
    MeshEditService& MeshEdit;
    MarqueeState& Marquee;
    GridSettings& Grid;
    EditorOverlayState& Overlay;
    // The brush-edit backend, for tools that preview/commit mesh edits (the edge cut).
    ManipulationSink& Sink;
    // What the person is holding: new brushes stamp the active material onto
    // their faces, Shift+RClick sampling writes it.
    ActiveResources& Resources;
    // Lets a tool hand off to another tool by id (e.g. the edge cut switches to
    // Select after committing). Set by the workspace once the registry exists.
    std::function<void(std::string_view)> ActivateTool;
};
