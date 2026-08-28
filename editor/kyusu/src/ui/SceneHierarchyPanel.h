#pragma once

#include "ui/IEditorPanel.h"

#include <ecs/EntityId.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

class CommandStack;
class EditorDocument;
class EditorScene;
class WorldDocument;
class SelectionService;
struct SelectableRef;

class SceneHierarchyPanel : public IEditorPanel
{
public:
    SceneHierarchyPanel(WorldDocument& world,
                        SelectionService& selection, CommandStack& commands);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Right; }

private:
    struct DrawContext;

    void DrawRow(DrawContext& ctx, EntityId entity, int depth);
    void HandleRowClick(DrawContext& ctx, EntityId entity);
    void HandleRowDragDrop(DrawContext& ctx, EntityId entity);
    void DrawRowContextMenu(DrawContext& ctx, EntityId entity);
    [[nodiscard]] bool RowMatchesFilter(const DrawContext& ctx, EntityId entity) const;
    [[nodiscard]] bool BranchMatchesFilter(const DrawContext& ctx, EntityId entity) const;

    WorldDocument& WorldDoc;
    SelectionService& Selection;
    CommandStack& Commands;
    // GetTitle is const and the title varies with the focus zone; the ### suffix
    // keeps the ImGui window identity stable while the visible text changes.
    mutable std::string TitleCache;

    // Rows the user closed, by persistent id so the choice survives the entity
    // handle changing under undo/redo and document reload. Absent means open:
    // a new branch appears expanded, which is where the entity the user just
    // made is.
    std::unordered_set<std::uint64_t> CollapsedIds;
    // Placement rows invert the default: an instance reads as one thing until
    // the user opens it, so this set records the ones opened.
    std::unordered_set<std::uint64_t> ExpandedInstanceIds;

    // In-progress rename, by persistent id (zero when idle).
    std::uint64_t RenamingId = 0;
    char RenameBuffer[64] = {};
    bool RenameFocusPending = false;

    char FilterText[64] = {};

    // The set being dragged. Panel state rather than frame state: the payload
    // lives across frames inside ImGui, and the drop can land on a frame where
    // the source row was scrolled out of the clip rect and never re-populated
    // a per-frame copy.
    std::vector<EntityId> DragSet;

    // The primary selection the panel last revealed; a change made outside the
    // panel (a viewport click) expands to and scrolls to the new one.
    EntityId LastRevealedPrimary = {};
};
