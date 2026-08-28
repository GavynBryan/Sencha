#include "SceneHierarchyPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "fonts/IconsFontAwesome6.h"

#include "commands/CommandStack.h"
#include "document/commands/CreateEntityCommand.h"
#include "document/commands/DeleteEntityCommand.h"
#include "document/commands/MoveEntitiesToZoneCommand.h"
#include "document/commands/RenameEntityCommand.h"
#include "document/commands/ReparentEntitiesCommand.h"
#include "document/commands/SceneInstanceCommands.h"
#include "document/commands/SetSceneOriginCommand.h"
#include "document/commands/ValueCommand.h"
#include "document/EditorScene.h"
#include "document/EntityNameComponent.h"
#include "document/WorldDocument.h"
#include "selection/commands/SelectCommand.h"
#include "selection/SelectionService.h"

#include <core/logging/LoggingProvider.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializer.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "document/DocumentSerialization.h"

namespace
{
    // The drag-drop payload is a marker; the dragged set itself is panel state.
    // ImGui copies payload bytes, and entity handles have no business being
    // memcpy'd through ImGui's allocator when the panel outlives the drag.
    constexpr const char* kDragPayloadType = "KYUSU_HIERARCHY_ENTITIES";

    [[nodiscard]] std::uint64_t PersistentOf(const EditorScene& scene, EntityId entity)
    {
        const auto* id =
            scene.GetRegistry().Components.TryGet<PersistentIdComponent>(entity);
        return id != nullptr ? id->Id.Value : 0;
    }

    [[nodiscard]] std::string RowLabel(const EditorScene& scene, EntityId entity)
    {
        const World& world = scene.GetRegistry().Components;
        if (const auto* name = world.TryGet<EntityNameComponent>(entity);
            name != nullptr && !name->Value.View().empty())
        {
            return std::string(name->Value.View());
        }

        // Registry-driven summary: the components present on this entity, named
        // by the serializer registry -- no hard-coded component list.
        std::string summary;
        for (const auto& serializer : EditorSceneSerializers().Entries())
        {
            const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
            if (id == InvalidComponentId || !world.HasComponent(entity, id))
                continue;
            if (!summary.empty())
                summary += ", ";
            summary += std::string(serializer->JsonKey());
        }
        if (summary.empty())
            return "Entity " + std::to_string(entity.Index);
        return summary + " " + std::to_string(entity.Index);
    }

    [[nodiscard]] bool ContainsCaseInsensitive(std::string_view haystack,
                                               std::string_view needle)
    {
        if (needle.empty())
            return true;
        const auto it = std::search(
            haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [](char a, char b)
            {
                return std::tolower(static_cast<unsigned char>(a))
                    == std::tolower(static_cast<unsigned char>(b));
            });
        return it != haystack.end();
    }
} // namespace

// Per-frame draw state: the scene being drawn, the child index built once at
// the top of the frame, and the row order actually shown (what a shift-range
// spans). Deferred mutations collect here so no command runs while the loop
// that would be invalidated by it is still walking.
struct SceneHierarchyPanel::DrawContext
{
    DrawContext(EditorDocument& document, EditorScene& scene)
        : Document(document)
        , Scene(scene)
        , Registry(scene.GetRegistry().Id)
    {
    }

    EditorDocument& Document;
    EditorScene& Scene;
    RegistryId Registry;
    std::vector<EntityId> Roots;
    std::vector<std::vector<EntityId>> Children; // parallel to Order
    std::vector<EntityId> Order;                 // every live entity, index for Children
    std::vector<EntityId> VisibleRows;           // rows drawn this frame, top to bottom
    bool FilterActive = false;

    // Deferred actions.
    std::vector<EntityId> ToDelete;
    ZoneId MoveTarget = {};
    EntityId DropParent = {};
    bool DropRequested = false;
    bool DropKeepLocal = false;

    [[nodiscard]] std::span<const EntityId> ChildrenOf(EntityId entity) const
    {
        for (std::size_t i = 0; i < Order.size(); ++i)
            if (Order[i] == entity)
                return Children[i];
        return {};
    }
};

SceneHierarchyPanel::SceneHierarchyPanel(WorldDocument& world,
                                         SelectionService& selection, CommandStack& commands)
    : WorldDoc(world)
    , Selection(selection)
    , Commands(commands)
{
}

std::string_view SceneHierarchyPanel::GetTitle() const
{
    TitleCache = "Hierarchy";
    if (WorldDoc.IsWorld())
    {
        const ZoneId focus = WorldDoc.FocusZone();
        for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
        {
            if (zone.Id != focus)
                continue;
            TitleCache += " - ";
            TitleCache += zone.Name;
            break;
        }
    }
    TitleCache += "###Hierarchy";
    return TitleCache;
}

bool SceneHierarchyPanel::RowMatchesFilter(const DrawContext& ctx, EntityId entity) const
{
    return ContainsCaseInsensitive(RowLabel(ctx.Scene, entity), FilterText);
}

bool SceneHierarchyPanel::BranchMatchesFilter(const DrawContext& ctx, EntityId entity) const
{
    if (RowMatchesFilter(ctx, entity))
        return true;
    for (EntityId child : ctx.ChildrenOf(entity))
        if (BranchMatchesFilter(ctx, child))
            return true;
    return false;
}

void SceneHierarchyPanel::HandleRowClick(DrawContext& ctx, EntityId entity)
{
    const SelectableRef ref = SelectableRef::EntitySelection(ctx.Registry, entity);
    const ImGuiIO& io = ImGui::GetIO();

    SelectionSnapshot next;
    next.Primary = ref;
    if (io.KeyShift)
    {
        // Range over the rows as shown, anchored at the current primary.
        const SelectableRef primary = Selection.GetPrimarySelection();
        const auto anchor = std::find_if(
            ctx.VisibleRows.begin(), ctx.VisibleRows.end(),
            [&](EntityId row) { return primary.IsValid() && row == primary.Entity; });
        const auto clicked = std::find(ctx.VisibleRows.begin(), ctx.VisibleRows.end(), entity);
        if (anchor != ctx.VisibleRows.end() && clicked != ctx.VisibleRows.end())
        {
            const auto first = std::min(anchor, clicked);
            const auto last = std::max(anchor, clicked);
            for (auto it = first; it <= last; ++it)
                next.Items.push_back(SelectableRef::EntitySelection(ctx.Registry, *it));
        }
        else
        {
            next.Items.push_back(ref);
        }
    }
    else if (io.KeyCtrl)
    {
        for (const SelectableRef& existing : Selection.GetSelection())
            if (!(existing == ref))
                next.Items.push_back(existing);
        if (next.Items.size() == Selection.GetSelection().size())
            next.Items.push_back(ref); // was absent: add
        else if (!next.Items.empty())
            next.Primary = next.Items.back(); // was present: removed; repair primary
        else
            next.Primary = SelectableRef{};
    }
    else
    {
        next.Items.push_back(ref);
    }

    Commands.Execute(std::make_unique<SelectCommand>(Selection, std::move(next)));
}

void SceneHierarchyPanel::HandleRowDragDrop(DrawContext& ctx, EntityId entity)
{
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover))
    {
        // Dragging a selected row drags the whole selection; an unselected row
        // drags alone without disturbing the selection.
        DragSet.clear();
        bool inSelection = false;
        for (const SelectableRef& ref : Selection.GetSelection())
            if (ref.IsEntity())
            {
                if (ref.Entity == entity)
                    inSelection = true;
                DragSet.push_back(ref.Entity);
            }
        if (!inSelection)
        {
            DragSet.clear();
            DragSet.push_back(entity);
        }

        int marker = 0;
        ImGui::SetDragDropPayload(kDragPayloadType, &marker, sizeof(marker));
        ImGui::TextUnformatted(DragSet.size() == 1
            ? RowLabel(ctx.Scene, DragSet.front()).c_str()
            : "multiple entities");
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget())
    {
        // Refusal happens at the target, while the user can still see why: a
        // drop that would put an entity under its own descendant never lands.
        bool legal = true;
        const char* refusal = "cannot parent an entity beneath its own descendant";
        for (EntityId dragged : DragSet)
        {
            if (dragged == entity || ctx.Scene.IsAncestorOf(dragged, entity))
            {
                legal = false;
                break;
            }
            if (ctx.Document.IsSceneInstanceMember(dragged))
            {
                legal = false;
                refusal = "linked to its scene source; use Break Scene "
                          "Instance to restructure";
                break;
            }
        }

        if (!legal)
        {
            ImGui::SetTooltip("%s", refusal);
            (void)ImGui::AcceptDragDropPayload(kDragPayloadType,
                                               ImGuiDragDropFlags_AcceptPeekOnly);
        }
        else if (ImGui::AcceptDragDropPayload(kDragPayloadType) != nullptr)
        {
            ctx.DropParent = entity;
            ctx.DropRequested = true;
            ctx.DropKeepLocal = ImGui::GetIO().KeyShift;
        }
        ImGui::EndDragDropTarget();
    }
}

void SceneHierarchyPanel::DrawRowContextMenu(DrawContext& ctx, EntityId entity)
{
    if (!ImGui::BeginPopupContextItem("##row_ctx"))
        return;

    if (ImGui::MenuItem(ICON_FA_PEN "  Rename"))
    {
        RenamingId = PersistentOf(ctx.Scene, entity);
        RenameFocusPending = true;
        const World& world = ctx.Scene.GetRegistry().Components;
        RenameBuffer[0] = '\0';
        if (const auto* name = world.TryGet<EntityNameComponent>(entity))
            std::snprintf(RenameBuffer, sizeof(RenameBuffer), "%s",
                          std::string(name->Value.View()).c_str());
    }

    if (ctx.Document.IsSceneInstanceRoot(entity)
        && ImGui::MenuItem(ICON_FA_LINK_SLASH "  Break Scene Instance"))
    {
        const World& world = ctx.Scene.GetRegistry().Components;
        if (const auto* id = world.TryGet<PersistentIdComponent>(entity))
            Commands.Execute(std::make_unique<BreakSceneInstanceCommand>(
                SceneInstanceId{ id->Id.Value }, ctx.Document));
    }

    if (ctx.Scene.GetParent(entity).IsValid()
        && ImGui::MenuItem(ICON_FA_ARROW_UP "  Clear Parent"))
    {
        const EntityId targets[] = { entity };
        if (auto command = MakeReparentEntitiesCommand(
                targets, EntityId{}, ReparentTransformRule::KeepWorld,
                ctx.Scene, ctx.Document))
            Commands.Execute(std::move(command));
    }

    ImGui::Separator();

    if (ImGui::MenuItem(ICON_FA_TRASH "  Delete"))
    {
        // Deleting a selected row deletes the selection; an unselected row
        // deletes alone.
        bool inSelection = false;
        for (const SelectableRef& ref : Selection.GetSelection())
            if (ref.IsEntity() && ref.Entity == entity)
                inSelection = true;
        if (inSelection)
        {
            for (const SelectableRef& ref : Selection.GetSelection())
                if (ref.IsEntity())
                    ctx.ToDelete.push_back(ref.Entity);
        }
        else
        {
            ctx.ToDelete.push_back(entity);
        }
    }

    if (WorldDoc.IsWorld())
    {
        // Targets: every open zone except the focus zone, manifest order.
        bool hasTarget = false;
        for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
            if (zone.Id != WorldDoc.FocusZone() && WorldDoc.IsZoneOpen(zone.Id))
                hasTarget = true;
        if (ImGui::BeginMenu(ICON_FA_ARROW_RIGHT "  Move To Zone", hasTarget))
        {
            for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
            {
                if (zone.Id == WorldDoc.FocusZone() || !WorldDoc.IsZoneOpen(zone.Id))
                    continue;
                if (ImGui::MenuItem(zone.Name.c_str()))
                    ctx.MoveTarget = zone.Id;
            }
            ImGui::EndMenu();
        }
        if (!hasTarget && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("load a target zone as context first");
    }

    ImGui::EndPopup();
}

void SceneHierarchyPanel::DrawRow(DrawContext& ctx, EntityId entity, int depth)
{
    // With a filter active, a branch with no match anywhere is pruned; a branch
    // with a deep match stays, ancestors included, or the match would appear to
    // live at the root.
    if (ctx.FilterActive && !BranchMatchesFilter(ctx, entity))
        return;

    const std::uint64_t pid = PersistentOf(ctx.Scene, entity);
    const std::span<const EntityId> children = ctx.ChildrenOf(entity);
    const bool selected = [&]
    {
        for (const SelectableRef& ref : Selection.GetSelection())
            if (ref.IsEntity() && ref.Entity == entity)
                return true;
        return false;
    }();

    ImGui::PushID(static_cast<int>(pid ^ (pid >> 32)));
    ctx.VisibleRows.push_back(entity);

    // Own-flag toggles, undoable now that the flags persist with the document.
    const bool visible = ctx.Scene.IsEntityVisible(entity);
    const bool locked = ctx.Scene.IsEntityLocked(entity);
    EditorScene& scene = ctx.Scene;
    if (ImGui::SmallButton(visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH))
        Commands.Execute(std::make_unique<ValueCommand<bool>>(
            visible, !visible,
            [&scene, entity](const bool& v) { scene.SetEntityVisible(entity, v); },
            ctx.Document));
    ImGui::SameLine();
    if (ImGui::SmallButton(locked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN))
        Commands.Execute(std::make_unique<ValueCommand<bool>>(
            locked, !locked,
            [&scene, entity](const bool& v) { scene.SetEntityLocked(entity, v); },
            ctx.Document));
    ImGui::SameLine();

    // Effectively hidden rows read dimmed -- their own flag or an ancestor's;
    // the eye button above still shows the row's own state.
    const bool dimmed = !ctx.Scene.IsEntityEffectivelyVisible(entity);
    if (dimmed)
        ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);

    const bool instanceRoot = ctx.Document.IsSceneInstanceRoot(entity);
    const bool instanceMember = ctx.Document.IsSceneInstanceMember(entity);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selected)
        flags |= ImGuiTreeNodeFlags_Selected;

    // Open state is panel-owned (persistent-id keyed), not ImGui-owned, so a
    // rebuilt entity keeps its expansion. A filter forces branches open so the
    // matches it kept are actually on screen. Placements invert the default:
    // an instance is one thing until deliberately opened.
    const bool open = ctx.FilterActive
        || (instanceRoot ? ExpandedInstanceIds.contains(pid)
                         : !CollapsedIds.contains(pid));
    if (!children.empty())
        ImGui::SetNextItemOpen(open);

    const char* icon = instanceRoot ? ICON_FA_BOX_OPEN "  "
                     : instanceMember ? ICON_FA_LINK "  "
                     : children.empty() ? ICON_FA_CUBE "  "
                                        : ICON_FA_CUBES "  ";
    std::string rowText = RowLabel(ctx.Scene, entity);
    if (instanceRoot
        && ctx.Scene.GetRegistry().Components.TryGet<EntityNameComponent>(entity)
               == nullptr)
    {
        // A nameless placement is called by its source, not by its components.
        const std::string source = ctx.Document.SceneInstanceSourceOf(entity);
        const std::size_t slash = source.find_last_of('/');
        const std::size_t stem = slash == std::string::npos ? sizeof("asset://") - 1
                                                            : slash + 1;
        std::string_view leaf(source);
        leaf.remove_prefix(stem);
        if (leaf.ends_with(".sscene"))
            leaf.remove_suffix(sizeof(".sscene") - 1);
        if (!leaf.empty())
            rowText = std::string(leaf);
    }
    const bool renaming = RenamingId != 0 && RenamingId == pid;
    const std::string label = renaming
        ? std::string("##renaming")
        : std::string(icon) + rowText + "##row";
    const bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);

    if (!children.empty() && nodeOpen != open && !ctx.FilterActive)
    {
        if (instanceRoot)
        {
            if (nodeOpen)
                ExpandedInstanceIds.insert(pid);
            else
                ExpandedInstanceIds.erase(pid);
        }
        else if (nodeOpen)
        {
            CollapsedIds.erase(pid);
        }
        else
        {
            CollapsedIds.insert(pid);
        }
    }

    if (!renaming && (instanceRoot || instanceMember)
        && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    {
        const std::string source = ctx.Document.SceneInstanceSourceOf(entity);
        if (!source.empty())
            ImGui::SetTooltip(instanceRoot ? "%s" : "%s (linked; break the "
                                                    "instance to restructure)",
                              source.c_str());
    }

    if (renaming)
    {
        ImGui::SameLine();
        if (RenameFocusPending)
        {
            ImGui::SetKeyboardFocusHere();
            RenameFocusPending = false;
        }
        const bool committed = ImGui::InputText(
            "##rename_edit", RenameBuffer, sizeof(RenameBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (committed || (ImGui::IsItemDeactivated() && !cancelled))
        {
            if (auto command = MakeRenameEntityCommand(entity, RenameBuffer,
                                                       ctx.Scene, ctx.Document))
                Commands.Execute(std::move(command));
            RenamingId = 0;
        }
        else if (cancelled)
        {
            RenamingId = 0;
        }
    }
    else
    {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)
            && !ImGui::IsItemToggledOpen())
        {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                RenamingId = pid;
                RenameFocusPending = true;
                RenameBuffer[0] = '\0';
                const World& world = ctx.Scene.GetRegistry().Components;
                if (const auto* name = world.TryGet<EntityNameComponent>(entity))
                    std::snprintf(RenameBuffer, sizeof(RenameBuffer), "%s",
                                  std::string(name->Value.View()).c_str());
            }
            else
            {
                HandleRowClick(ctx, entity);
            }
        }
        HandleRowDragDrop(ctx, entity);
        DrawRowContextMenu(ctx, entity);
    }

    if (dimmed)
        ImGui::PopStyleColor();

    // Reveal a selection made elsewhere: scroll to it once when it changes.
    if (selected && entity == LastRevealedPrimary && !ImGui::IsItemVisible())
        ImGui::SetScrollHereY(0.35f);

    if (!children.empty() && nodeOpen)
    {
        for (EntityId child : children)
            DrawRow(ctx, child, depth + 1);
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void SceneHierarchyPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    EditorDocument& document = WorldDoc.FocusDocument();
    EditorScene& scene = document.GetScene();

    DrawContext ctx(document, scene);

    // Create a plain entity (Transform only) and select it; the inspector adds
    // game components to it. This is the non-brush authoring path.
    if (ImGui::Button(ICON_FA_PLUS "  New Entity"))
    {
        auto create = MakeCreateEntityCommand(Vec3d::Zero(), scene, document);
        CreateEntityCommand* cmd = create.get();
        Commands.Execute(std::move(create));
        Commands.Execute(std::make_unique<SelectCommand>(
            Selection, SelectableRef::EntitySelection(ctx.Registry, cmd->GetCreatedEntity())));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS "  filter",
                             FilterText, sizeof(FilterText));
    ctx.FilterActive = FilterText[0] != '\0';
    ImGui::Separator();

    // One pass over the tracked list builds the tree shape for the frame.
    ctx.Order.assign(scene.GetAllEntities().begin(), scene.GetAllEntities().end());
    ctx.Children.resize(ctx.Order.size());
    for (EntityId entity : ctx.Order)
    {
        const EntityId parent = scene.GetParent(entity);
        if (!parent.IsValid() || !scene.HasEntity(parent))
        {
            ctx.Roots.push_back(entity);
            continue;
        }
        for (std::size_t i = 0; i < ctx.Order.size(); ++i)
            if (ctx.Order[i] == parent)
            {
                ctx.Children[i].push_back(entity);
                break;
            }
    }

    // A selection made outside the panel gets revealed: expand its ancestry so
    // the scroll in DrawRow has a row to land on.
    const SelectableRef primary = Selection.GetPrimarySelection();
    if (primary.IsValid() && primary.Entity != LastRevealedPrimary
        && scene.HasEntity(primary.Entity))
    {
        LastRevealedPrimary = primary.Entity;
        for (EntityId ancestor = scene.GetParent(primary.Entity);
             ancestor.IsValid();
             ancestor = scene.GetParent(ancestor))
        {
            const std::uint64_t ancestorPid = PersistentOf(scene, ancestor);
            CollapsedIds.erase(ancestorPid);
            if (document.IsSceneInstanceRoot(ancestor))
                ExpandedInstanceIds.insert(ancestorPid);
        }
    }

    // The scene root: the drop target that unparents, and the anchor the whole
    // tree hangs from so "drop between top-level rows" has somewhere legal to
    // land.
    ImGui::TextDisabled(ICON_FA_MAP "  Scene");
    if (ImGui::BeginPopupContextItem("##scene_root_ctx"))
    {
        // Re-origin the source so it places well: the selection's world
        // position becomes the scene's origin, which is the point a placement
        // of this scene will pivot around.
        const SelectableRef target = Selection.GetPrimarySelection();
        const bool haveTarget = target.IsValid() && scene.HasEntity(target.Entity);
        if (ImGui::MenuItem(ICON_FA_LOCATION_CROSSHAIRS "  Set Origin to Selection",
                            nullptr, false, haveTarget))
        {
            if (auto command = MakeSetSceneOriginCommand(
                    scene, document,
                    scene.ComposeWorldTransform(target.Entity).Position))
                Commands.Execute(std::move(command));
        }
        if (!haveTarget && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("select the entity that should sit at the origin");
        ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (ImGui::AcceptDragDropPayload(kDragPayloadType) != nullptr)
        {
            ctx.DropParent = EntityId{};
            ctx.DropRequested = true;
            ctx.DropKeepLocal = ImGui::GetIO().KeyShift;
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::Indent();

    for (EntityId root : ctx.Roots)
        DrawRow(ctx, root, 0);

    ImGui::Unindent();

    // Deferred mutations, now that no loop over the entity list is live.
    if (ctx.DropRequested && !DragSet.empty())
    {
        if (auto command = MakeReparentEntitiesCommand(
                DragSet, ctx.DropParent,
                ctx.DropKeepLocal ? ReparentTransformRule::KeepLocal
                                  : ReparentTransformRule::KeepWorld,
                scene, document))
            Commands.Execute(std::move(command));
    }

    if (!ctx.ToDelete.empty())
        Commands.Execute(MakeDeleteEntitiesCommand(ctx.ToDelete, scene, document, Selection));

    if (ctx.MoveTarget.IsValid())
    {
        // The move routes the current entity selection, matching the panel
        // acting on selection state rather than the hovered row alone.
        std::vector<EntityId> selectedEntities;
        for (const SelectableRef& ref : Selection.GetSelection())
            if (ref.IsEntity())
                selectedEntities.push_back(ref.Entity);
        if (auto command = MakeMoveEntitiesToZoneCommand(selectedEntities, WorldDoc,
                                                         ctx.MoveTarget, Selection))
        {
            const size_t count = selectedEntities.size();
            Commands.Execute(std::move(command));
            for (const ZoneHeader& zone : WorldDoc.Manifest().Zones)
                if (zone.Id == ctx.MoveTarget)
                    WorldDoc.Logging().GetLogger<SceneHierarchyPanel>().Info(
                        "moved {} entities to {}", count, zone.Name);
        }
    }
}
