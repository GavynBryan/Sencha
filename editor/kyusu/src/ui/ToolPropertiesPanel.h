#pragma once

#include "FaceUvControls.h"
#include "ui/IEditorPanel.h"

#include <functional>
#include <optional>

class CommandStack;
class MeshEditService;
class SelectionService;
class ToolRegistry;
class WorldDocument;
struct ActiveMaterialState;
struct BrushCreationSettings;
struct EdgeCutSettings;
struct IMeshEditTarget;

// The active tool's contextual properties: brush-create parameters, edge-cut
// placement, face-carve apply/cancel, and the per-element-mode verbs of the
// Select tool (extrude, delete, cuts, bake/revert/export for objects). Mode and
// gizmo switching live in the top toolbar; this panel hosts properties and
// verbs. All mesh edits go through MeshEditService against the injected edit
// target; the panel never touches BrushOps or the scene directly.
class ToolPropertiesPanel : public IEditorPanel
{
public:
    // Host wiring for the object verbs that need scene/asset access (the panel
    // stays scene- and asset-agnostic). Any callback may be empty; the buttons
    // show only for applicable selections.
    struct ObjectActions
    {
        std::function<void()> Duplicate;   // independent copies (Ctrl+D)
        std::function<void()> Instance;    // copies SHARING the source mesh (Alt+D)
        std::function<void()> MakeUnique;  // break selected brushes out of their instance groups
        std::function<void()> Merge;       // join selected brushes into the primary
        std::function<void()> SeparateFaces; // split the selected faces into a new brush
        std::function<void()> Bake;        // selected brushes -> .smesh + component swap
        std::function<void()> Revert;      // selected baked entities -> brushes again
        std::function<void()> ExportGlb;   // selected brush/baked mesh -> .glb on disk
        std::function<void(int)> BridgeEdges; // selected edge paths, including cross-brush surfaces
        // Cross-brush bridge pending state: the bridge previews as a live
        // entity; Segments regenerates it, Apply commits, Cancel drops it.
        std::function<bool()> HasPendingBridge;
        std::function<void(int)> SetBridgeSegments;
        std::function<void()> CommitBridge;
        std::function<void()> CancelBridge;
        // Pending inset (faces) / bevel (edges): begin previews on the
        // selection, the setters regenerate live, commit/cancel close it.
        std::function<void(float)> BeginInset;
        std::function<bool()> HasPendingInset;
        std::function<void(float)> SetInsetDistance;
        std::function<void(float, int)> BeginBevel;
        std::function<bool()> HasPendingBevel;
        std::function<void(float, int)> SetBevelParams;
        std::function<void()> CommitElementEdit;
        std::function<void()> CancelElementEdit;
        std::function<bool()> HasBakedSelection;     // any selected entity with a dormant brush
        std::function<bool()> HasInstancedSelection; // any selected instanced brush
    };

    // The edit target (the workspace's manipulation sink) is rebuilt on focus
    // change, so the composition root injects a resolver instead of a reference.
    // The tool registry is resolver-injected too: it is created after the panels.
    ToolPropertiesPanel(std::function<IMeshEditTarget*()> target,
                        std::function<ToolRegistry*()> tools,
                        SelectionService& selection,
                        MeshEditService& meshEdit,
                        CommandStack& commands,
                        WorldDocument& world,
                        ActiveMaterialState& activeMaterial,
                        std::optional<FaceMaterialClipboard>& uvClipboard,
                        BrushCreationSettings& brushCreate,
                        EdgeCutSettings& edgeCut,
                        ObjectActions objectActions);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Left; }
    // Shares the left column with the Active Material panel; this keeps the
    // larger share.
    float GetDockWeight() const override { return 1.8f; }

private:
    void DrawSelectProperties();
    void DrawBrushProperties();
    void DrawEdgeCutProperties();
    void DrawFaceCarveProperties();
    void DrawObjectVerbs();
    void DrawFaceVerbs();
    void DrawEdgeVerbs();
    void DrawVertexVerbs();
    void DrawTextureTools();

    [[nodiscard]] IMeshEditTarget& Target() const;
    [[nodiscard]] ToolRegistry* Tools() const;

    std::function<IMeshEditTarget*()> TargetResolver;
    std::function<ToolRegistry*()> ToolsResolver;
    SelectionService& Selection;
    MeshEditService& MeshEdit;
    CommandStack& Commands;
    WorldDocument& World;
    ActiveMaterialState& ActiveMaterial;
    std::optional<FaceMaterialClipboard>& UvClipboard;
    BrushCreationSettings& BrushCreate;
    EdgeCutSettings& EdgeCut;
    FaceUvControls Uv;
    ObjectActions Objects;

    float WeldDistance = 0.1f; // vertex weld: max merge distance (local units)
    int BridgeSegments = 1;    // bridge: quad rows spanning the gap
    bool BridgeOptionsOpen = false; // bridge options expanded (collapsed to a button otherwise)
    float InsetDistance = 0.25f; // face inset: distance along the blended normals
    float BevelWidth = 0.25f;    // edge bevel: retreat into each face
    int BevelSegments = 1;       // edge bevel: chamfer strips across the profile
};
