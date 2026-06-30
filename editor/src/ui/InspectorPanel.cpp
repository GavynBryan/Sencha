#include "InspectorPanel.h"

#include "EditorUiStyle.h"
#include "ScopedPanel.h"
#include "fonts/IconsFontAwesome6.h"

#include "../commands/CommandStack.h"
#include "../document/AssetFieldIo.h"
#include "../document/commands/AssetFieldEditCommand.h"
#include "../document/commands/RawComponentEditCommand.h"
#include "../document/commands/RawComponentAddCommand.h"
#include "../document/commands/RawComponentRemoveCommand.h"
#include "../document/EditorDocument.h"
#include "../selection/SelectionService.h"

#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <core/metadata/RuntimeSchema.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializer.h>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace
{
    ImGuiDataType DataTypeFor(const RuntimeField& field)
    {
        switch (field.Scalar)
        {
        case FieldScalar::Float:  return ImGuiDataType_Float;
        case FieldScalar::Double: return ImGuiDataType_Double;
        case FieldScalar::Int32:
            return field.Size == 1 ? ImGuiDataType_S8
                 : field.Size == 2 ? ImGuiDataType_S16
                 : field.Size >= 8 ? ImGuiDataType_S64
                                   : ImGuiDataType_S32;
        case FieldScalar::UInt32:
            return field.Size == 1 ? ImGuiDataType_U8
                 : field.Size == 2 ? ImGuiDataType_U16
                 : field.Size >= 8 ? ImGuiDataType_U64
                                   : ImGuiDataType_U32;
        default:
            return ImGuiDataType_S32;
        }
    }

    struct FieldEdit
    {
        bool Activated = false; // widget gained focus this frame (pre-edit)
        bool Committed = false; // edit finished this frame
    };

    // Lays out one row as [label column | widget column], the widget filling the
    // rest of the width — the two-column inspector look. The widget uses a hidden
    // ("##") label so only the left-column text shows.
    FieldEdit DrawRuntimeField(const RuntimeField& field, std::byte* component, float labelWidth)
    {
        FieldEdit edit;
        void* ptr = component + field.Offset;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(field.Name.c_str());
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-FLT_MIN);

        const std::string id = "##" + field.Name;
        if (field.Scalar == FieldScalar::Unsupported)
        {
            ImGui::TextDisabled("<unsupported>");
            return edit;
        }

        if (field.Scalar == FieldScalar::Bool)
            ImGui::Checkbox(id.c_str(), reinterpret_cast<bool*>(ptr));
        else if (field.Scalar == FieldScalar::Color3)
            ImGui::ColorEdit3(id.c_str(), reinterpret_cast<float*>(ptr),
                              ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        else
            ImGui::DragScalar(id.c_str(), DataTypeFor(field), ptr, 0.05f);

        edit.Activated = ImGui::IsItemActivated();
        edit.Committed = ImGui::IsItemDeactivatedAfterEdit();
        return edit;
    }
}

InspectorPanel::InspectorPanel(EditorScene& scene,
                               EditorDocument& document,
                               SelectionService& selection,
                               CommandStack& commands)
    : Scene(scene)
    , Document(document)
    , Selection(selection)
    , Commands(commands)
{
}

std::string_view InspectorPanel::GetTitle() const
{
    return "Inspector";
}

void InspectorPanel::ResetEditState()
{
    EditActive = false;
    EditingComponent = InvalidComponentId;
    EditBefore.clear();
}

void InspectorPanel::DrawComponent(IComponentSerializer& serializer, EntityId entity)
{
    World& world = Scene.GetRegistry().Components;
    const ComponentId id = world.GetComponentIdByType(serializer.TypeId());
    if (id == InvalidComponentId)
        return;

    const ComponentMeta* meta = world.GetMeta(id);
    const std::size_t size = meta ? meta->Size : 0;

    const std::string key(serializer.JsonKey());
    // Stable id from the JsonKey; the icon is display-only (kept out of the id).
    const std::string header = std::string(ICON_FA_CUBES "  ") + key + "###" + key;
    // Let the trash button below sit on top of the full-width header and take its
    // own clicks (otherwise the header swallows them as a collapse toggle).
    ImGui::SetNextItemAllowOverlap();
    const bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

    // Remove affordances on the header row: a right-click context menu (bound to
    // the header, so registered before any later same-row item) and a right-
    // aligned trash button. Both defer to PendingRemoval, executed after the loop.
    // Suppressed for components the registry marks non-removable (the transform).
    if (serializer.IsRemovable())
    {
        if (ImGui::BeginPopupContextItem(("##ctx_" + key).c_str()))
        {
            if (ImGui::MenuItem(ICON_FA_TRASH "  Remove Component"))
                PendingRemoval = &serializer;
            ImGui::EndPopup();
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
        if (ImGui::SmallButton((std::string(ICON_FA_TRASH) + "##del_" + key).c_str()))
            PendingRemoval = &serializer;
    }

    if (!open)
        return;

    ImGui::PushID(key.c_str());

    // Work on a copy of the component's bytes so reads don't churn change
    // tracking every frame; write back only when a widget actually edits.
    std::vector<std::byte> working(size);
    if (size > 0)
    {
        const void* live = world.GetComponentRaw(entity, id);
        if (live == nullptr)
        {
            ImGui::PopID();
            return;
        }
        std::memcpy(working.data(), live, size);
    }
    const std::vector<std::byte> frameStart = working;

    // Label column ~42% of the section width, clamped, so widgets line up.
    const float labelWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.42f, 70.0f, 200.0f);

    bool activated = false;
    bool committed = false;
    for (const RuntimeField& field : serializer.RuntimeFields())
    {
        // Asset-handle fields resolve through the asset system, not raw scalar
        // bytes: the picker reads/writes the live component directly via its own
        // undoable command, so it sits outside the working-copy scalar path.
        if (field.Asset != AssetType::Unknown)
        {
            DrawAssetField(field, entity, id, labelWidth);
            continue;
        }
        const FieldEdit edit = DrawRuntimeField(field, working.data(), labelWidth);
        activated |= edit.Activated;
        committed |= edit.Committed;
    }

    // Begin an undoable edit: snapshot the pre-edit bytes on widget activation.
    // Refresh on every activation (only one widget is active at a time) so a
    // click-release without an edit can't leave a stale snapshot behind.
    if (activated)
    {
        EditActive = true;
        EditingComponent = id;
        EditBefore = frameStart;
    }

    // Apply this frame's edit to the live component for immediate feedback.
    if (size > 0 && std::memcmp(working.data(), frameStart.data(), size) != 0)
    {
        if (void* live = world.GetComponentRaw(entity, id))
            std::memcpy(live, working.data(), size);
    }

    // Commit: record one undoable command spanning the whole drag.
    if (committed && EditActive && EditingComponent == id)
    {
        Commands.Execute(std::make_unique<RawComponentEditCommand>(
            entity, id, EditBefore, working, Scene, Document));
        EditActive = false;
        EditingComponent = InvalidComponentId;
    }

    ImGui::PopID();
}

namespace
{
    struct CatalogEntry { std::string Path; AssetId Id; };

    // Stable, sorted assets of one kind (AssetRegistry::Records() is unordered).
    std::vector<CatalogEntry> SortedAssetsOfType(const AssetRegistry& catalog, AssetType type)
    {
        std::vector<CatalogEntry> entries;
        for (const auto& entry : catalog.Records())
            if (entry.second.Type == type)
                entries.push_back({ entry.first, entry.second.Id });
        std::sort(entries.begin(), entries.end(),
                  [](const CatalogEntry& a, const CatalogEntry& b) { return a.Path < b.Path; });
        return entries;
    }

    // One asset picker combo. Returns true and fills `picked` when the user
    // chooses a different entry ("(none)" yields an empty ref). `widgetId` must
    // be unique, so list slots push an index id around it.
    bool AssetPickCombo(const char* widgetId, const AssetFieldRef& current,
                        const std::vector<CatalogEntry>& entries, AssetFieldRef& picked)
    {
        bool changed = false;
        const char* preview = current.Path.empty() ? "(none)" : current.Path.c_str();
        if (ImGui::BeginCombo(widgetId, preview))
        {
            if (ImGui::Selectable("(none)", current.Path.empty()) && !current.Path.empty())
            {
                picked = AssetFieldRef{};
                changed = true;
            }
            for (const CatalogEntry& entry : entries)
            {
                const bool selected = (entry.Path == current.Path);
                if (ImGui::Selectable(entry.Path.c_str(), selected) && !selected)
                {
                    picked = AssetFieldRef{ entry.Id, entry.Path };
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }
}

void InspectorPanel::DrawAssetField(const RuntimeField& field, EntityId entity,
                                    ComponentId component, float labelWidth)
{
    AssetSystem* assets = Document.GetAssetSystem();
    const AssetRegistry* catalog = Document.GetAssetCatalog();
    if (assets == nullptr || catalog == nullptr)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(field.Name.c_str());
        ImGui::SameLine(labelWidth);
        ImGui::TextDisabled("<no asset system>");
        return;
    }

    World& world = Scene.GetRegistry().Components;
    const void* base = world.GetComponentRaw(entity, component);
    if (base == nullptr)
        return;
    const void* fieldPtr = static_cast<const std::byte*>(base) + field.Offset;
    const AssetFieldValue current = ReadAssetField(*assets, field.Asset, field.Arity, fieldPtr);

    const std::vector<CatalogEntry> entries = SortedAssetsOfType(*catalog, field.Asset);

    // One edit = one full before/after value through the refcount-balanced command.
    const auto apply = [&](AssetFieldValue next) {
        Commands.Execute(std::make_unique<AssetFieldEditCommand>(
            entity, component, field.Offset, field.Asset, field.Arity,
            current, std::move(next), Scene, Document, *assets));
    };

    if (field.Arity != AssetArity::List)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(field.Name.c_str());
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-FLT_MIN);

        const AssetFieldRef cur = current.Refs.empty() ? AssetFieldRef{} : current.Refs.front();
        AssetFieldRef picked;
        if (AssetPickCombo(("##" + field.Name).c_str(), cur, entries, picked))
        {
            AssetFieldValue next;
            if (!picked.Path.empty())
                next.Refs.push_back(std::move(picked));
            apply(std::move(next));
        }
        return;
    }

    // List arity (per-slot materials): an ordered slot per index. The slot count
    // is the authored set length, free to grow or shrink; a mesh section past the
    // end falls back to the last member at render time (StaticMeshComponent).
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(field.Name.c_str());

    ImGui::PushID(field.Name.c_str());
    ImGui::Indent();
    const float trim = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    for (std::size_t i = 0; i < current.Refs.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Slot %zu", i);
        ImGui::SameLine(labelWidth);

        ImGui::SetNextItemWidth(-trim);
        AssetFieldRef picked;
        if (AssetPickCombo("##slot", current.Refs[i], entries, picked))
        {
            AssetFieldValue next = current;
            next.Refs[i] = std::move(picked);
            apply(std::move(next));
        }
        ImGui::SameLine();
        if (ImGui::Button("X", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
        {
            AssetFieldValue next = current;
            next.Refs.erase(next.Refs.begin() + static_cast<std::ptrdiff_t>(i));
            apply(std::move(next));
        }
        ImGui::PopID();
    }
    if (ImGui::Button("+ Add slot"))
    {
        AssetFieldValue next = current;
        next.Refs.emplace_back();
        apply(std::move(next));
    }
    ImGui::Unindent();
    ImGui::PopID();
}

void InspectorPanel::DrawAddComponentMenu(EntityId entity)
{
    World& world = Scene.GetRegistry().Components;

    // OpenPopup only sets state; BeginPopup must run every frame or ImGui closes
    // the popup before a selection can be made.
    if (ImGui::Button(ICON_FA_PLUS "  Add Component"))
        ImGui::OpenPopup("##add_component");

    if (ImGui::BeginPopup("##add_component"))
    {
        bool anyAddable = false;
        for (const auto& serializer : GetComponentSerializerEntries())
        {
            const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
            if (id == InvalidComponentId || world.HasComponent(entity, id))
                continue;

            anyAddable = true;
            const std::string label(serializer->JsonKey());
            if (ImGui::Selectable(label.c_str()))
            {
                Commands.Execute(std::make_unique<RawComponentAddCommand>(
                    entity, id, serializer->DefaultBytes(), Scene, Document));
            }
        }

        if (!anyAddable)
            ImGui::TextDisabled("All components present");

        ImGui::EndPopup();
    }
}

void InspectorPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    const SelectableRef selection = Selection.GetPrimarySelection();
    const EntityId entity = selection.Entity;

    if (!selection.IsValid() || !Scene.HasEntity(entity))
    {
        ImGui::TextDisabled("No selection");
        ResetEditState();
        LastEntity = {};
        return;
    }

    if (!(entity == LastEntity))
    {
        ResetEditState();
        LastEntity = entity;
    }

    // Plain accent title (the glow is reserved for panel titles now).
    char title[64];
    std::snprintf(title, sizeof(title), ICON_FA_CUBE "  Entity %u", entity.Index);
    ImGui::TextColored(EditorUi::Accent, "%s", title);
    ImGui::SameLine();
    ImGui::TextDisabled("(gen %u)", entity.Generation);
    ImGui::Separator();

    // Registry-driven: every component the registry knows about, drawn by schema.
    // No component is named in editor code here.
    World& world = Scene.GetRegistry().Components;
    for (const auto& serializer : GetComponentSerializerEntries())
    {
        const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
        if (id != InvalidComponentId && world.HasComponent(entity, id))
            DrawComponent(*serializer, entity);
    }

    // Deferred: removal is a structural change, so it runs after the loop above
    // (which iterates serializers and reads live component bytes) has finished.
    if (PendingRemoval != nullptr)
    {
        Commands.Execute(std::make_unique<RawComponentRemoveCommand>(
            entity, *PendingRemoval, Scene, Document));
        PendingRemoval = nullptr;
        ResetEditState();
    }

    ImGui::Separator();
    DrawAddComponentMenu(entity);
}
