#pragma once

#include "ui/IEditorPanel.h"

#include <ecs/EntityId.h>

#include <cstdint>
#include <functional>
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
    // openSceneSource opens the .sscene an asset:// path names (the
    // instance rows' Open Source item); false when it cannot be resolved.
    SceneHierarchyPanel(WorldDocument& world,
                        SelectionService& selection, CommandStack& commands,
                        std::function<bool(const std::string&)> openSceneSource);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Right; }

private:
    struct DrawContext;

    void DrawRow(DrawContext& ctx, EntityId entity, int depth,
                 bool ancestorsVisible);
    // The row's own visibility and lock flags, as two undoable toggles. Own
    // state, not effective: the row dims for an ancestor's hidden flag, but
    // these buttons only ever report and set the entity's own.
    void DrawRowFlagToggles(DrawContext& ctx, EntityId entity);
    // What the row is called: an icon for what it is, then its name -- or its
    // source's, for a placement nobody has named.
    [[nodiscard]] static std::string RowLabelText(DrawContext& ctx, EntityId entity,
                                                  bool instanceRoot, bool instanceMember,
                                                  bool leaf);
    // The inline rename field, live while this row is the one being renamed.
    // Commits on Enter or focus loss, abandons on Escape.
    void DrawRowRename(DrawContext& ctx, EntityId entity);
    // Expansion is panel-owned and keyed on persistent id, so a rebuilt entity
    // keeps it. A placement inverts the default: an instance reads as one
    // thing until somebody opens it.
    [[nodiscard]] bool RowOpenState(std::uint64_t pid, bool instanceRoot,
                                    bool filterActive) const;
    void RememberRowOpenState(std::uint64_t pid, bool instanceRoot, bool nodeOpen);
    void HandleRowClick(DrawContext& ctx, EntityId entity);
    void HandleRowDragDrop(DrawContext& ctx, EntityId entity);
    // The between-rows half of the drop gesture: a thin target that inserts
    // the dragged block as a sibling under `parent`, immediately before
    // `before` (invalid = last). Drawn only while a hierarchy drag is live.
    void DrawInsertionSlot(DrawContext& ctx, EntityId parent, EntityId before);
    void DrawRowContextMenu(DrawContext& ctx, EntityId entity);
    [[nodiscard]] bool RowMatchesFilter(DrawContext& ctx, EntityId entity) const;
    [[nodiscard]] bool BranchMatchesFilter(DrawContext& ctx, EntityId entity) const;

    WorldDocument& WorldDoc;
    SelectionService& Selection;
    CommandStack& Commands;
    std::function<bool(const std::string&)> OpenSceneSource;
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
