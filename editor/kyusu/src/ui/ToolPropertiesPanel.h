#pragma once

#include "workspace/PendingBridgeEdit.h"
#include "workspace/PendingElementEdit.h"
#include "workspace/SelectionActions.h"

#include "FaceUvControls.h"
#include "ui/IEditorPanel.h"

#include <functional>
#include <optional>

class CommandStack;
class MeshEditService;
class SelectionService;
class ToolRegistry;
struct ManipulationSink;
class WorldDocument;
struct ActiveMaterialState;
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
    // The edit target (the workspace's manipulation sink) is rebuilt on focus
    // change, so the composition root injects a resolver instead of a reference.
    // The tool registry is resolver-injected too: it is created after the panels.
    ToolPropertiesPanel(std::function<IMeshEditTarget*()> target,
                        std::function<ManipulationSink*()> sink,
                        std::function<ToolRegistry*()> tools,
                        SelectionService& selection,
                        MeshEditService& meshEdit,
                        CommandStack& commands,
                        WorldDocument& world,
                        ActiveMaterialState& activeMaterial,
                        std::optional<FaceMaterialClipboard>& uvClipboard,
                        SelectionActions& actions,
                        PendingBridgeEdit& bridgeEdit,
                        PendingElementEdit& elementEdit,
                        std::function<void()> exportGlb);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Left; }
    // Shares the left column with the Active Material panel; this keeps the
    // larger share.
    float GetDockWeight() const override { return 1.8f; }

private:
    void DrawSelectProperties();
    void DrawObjectVerbs();
    void DrawFaceVerbs();
    void DrawEdgeVerbs();
    void DrawVertexVerbs();
    void DrawTextureTools();

    [[nodiscard]] IMeshEditTarget& Target() const;

    // The pending-edit verbs the panel's buttons drive. They need the edit sink,
    // which is rebuilt per document, so they resolve it at the call.
    void BeginInset(float distance);
    void BeginBevel(float width, int segments);
    void CommitElementEdit();
    void CancelElementEdit();
    void CommitBridge();
    void CancelBridge();
    [[nodiscard]] ToolRegistry* Tools() const;

    std::function<IMeshEditTarget*()> TargetResolver;
    std::function<ManipulationSink*()> SinkResolver;
    std::function<ToolRegistry*()> ToolsResolver;
    SelectionService& Selection;
    MeshEditService& MeshEdit;
    CommandStack& Commands;
    WorldDocument& World;
    ActiveMaterialState& ActiveMaterial;
    std::optional<FaceMaterialClipboard>& UvClipboard;
    FaceUvControls Uv;
    // The workspace mechanisms this panel drives. Narrow by design: it reads
    // and drives exactly these, rather than a bag of callbacks assembled for it.
    SelectionActions& Actions;
    PendingBridgeEdit& BridgeEdit;
    PendingElementEdit& ElementEdit;
    // Export opens a native file dialog, which is the shell's job, so it stays
    // a callback rather than another mechanism reference.
    std::function<void()> ExportGlb;

    float WeldDistance = 0.1f; // vertex weld: max merge distance (local units)
    int BridgeSegments = 1;    // bridge: quad rows spanning the gap
    bool BridgeOptionsOpen = false; // bridge options expanded (collapsed to a button otherwise)
    float InsetDistance = 0.25f; // face inset: distance along the blended normals
    float BevelWidth = 0.25f;    // edge bevel: retreat into each face
    int BevelSegments = 1;       // edge bevel: chamfer strips across the profile
};
