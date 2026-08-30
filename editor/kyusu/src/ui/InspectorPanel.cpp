#include "InspectorPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "fonts/IconsFontAwesome6.h"

#include "commands/CommandStack.h"
#include "authoring/EditorComponentAdapter.h"
#include "document/AssetFieldIo.h"
#include "document/DerivedComponents.h"
#include "document/commands/AssetFieldEditCommand.h"
#include "document/commands/RawComponentEditCommand.h"
#include "document/commands/RawComponentAddCommand.h"
#include "document/commands/RawComponentRemoveCommand.h"
#include "document/EditorDocument.h"
#include "document/WorldDocument.h"
#include "selection/SelectionService.h"

#include <core/assets/AssetRegistry.h>
#include <assets/data/DataAssetSubtype.h>
#include <assets/runtime/AssetSystem.h>
#include <core/metadata/RuntimeSchema.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializer.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include "document/DocumentSerialization.h"

namespace
{
    // Row label from a dotted schema path: last segment, '_' to space, each word
    // capitalized ("local.position" -> "Position", "play_on_active" -> "Play On
    // Active"). Display-only; the widget id and serialization keep the raw path.
    std::string HumanizeFieldLabel(const std::string& dotted)
    {
        const std::size_t dot = dotted.find_last_of('.');
        std::string out = dotted.substr(dot == std::string::npos ? 0 : dot + 1);
        bool boundary = true;
        for (char& ch : out)
        {
            if (ch == '_')
            {
                ch = ' ';
                boundary = true;
                continue;
            }
            if (boundary && ch >= 'a' && ch <= 'z')
                ch = static_cast<char>(ch - 'a' + 'A');
            boundary = false;
        }
        return out;
    }

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
        case FieldScalar::UInt64: return ImGuiDataType_U64;
        default:
            return ImGuiDataType_S32;
        }
    }

    struct FieldEdit
    {
        bool Activated = false; // widget gained focus this frame (pre-edit)
        bool Committed = false; // edit finished this frame
    };

    // Enum leaves store the underlying integer at the field's offset; these
    // read/write it through the declared size and signedness so a combo edit
    // never over/under-writes the component's bytes.
    std::int64_t ReadEnumUnderlying(const void* ptr, const RuntimeField& field)
    {
        const bool isSigned = field.Scalar == FieldScalar::Int32;
        switch (field.Size)
        {
        case 1: return isSigned ? static_cast<std::int64_t>(*static_cast<const std::int8_t*>(ptr))
                                : static_cast<std::int64_t>(*static_cast<const std::uint8_t*>(ptr));
        case 2: return isSigned ? static_cast<std::int64_t>(*static_cast<const std::int16_t*>(ptr))
                                : static_cast<std::int64_t>(*static_cast<const std::uint16_t*>(ptr));
        case 8: return *static_cast<const std::int64_t*>(ptr);
        default: return isSigned ? static_cast<std::int64_t>(*static_cast<const std::int32_t*>(ptr))
                                 : static_cast<std::int64_t>(*static_cast<const std::uint32_t*>(ptr));
        }
    }

    void WriteEnumUnderlying(void* ptr, const RuntimeField& field, std::int64_t value)
    {
        switch (field.Size)
        {
        case 1: *static_cast<std::uint8_t*>(ptr) = static_cast<std::uint8_t>(value); break;
        case 2: *static_cast<std::uint16_t*>(ptr) = static_cast<std::uint16_t>(value); break;
        case 8: *static_cast<std::int64_t*>(ptr) = value; break;
        default: *static_cast<std::uint32_t*>(ptr) = static_cast<std::uint32_t>(value); break;
        }
    }

    // What a selector shows for one option: its declared display name, else
    // the humanized persisted string. Selection always writes the value; the
    // persisted string never changes with the display.
    std::string EnumOptionLabel(const EnumOption& option)
    {
        return option.Display.empty() ? HumanizeFieldLabel(std::string(option.Name))
                                      : std::string(option.Display);
    }

    // A combo edit is atomic: selecting an option both begins and commits the
    // edit in one frame, unlike a drag's activate/deactivate pair.
    FieldEdit DrawEnumField(const RuntimeField& field, void* ptr, const std::string& id)
    {
        FieldEdit edit;
        const std::int64_t current = ReadEnumUnderlying(ptr, field);

        std::string preview;
        for (const EnumOption& option : field.Enum)
            if (option.Value == current)
                preview = EnumOptionLabel(option);
        // A value outside the schema's table (hand-edited data, removed
        // enumerator) shows as its raw number rather than pretending.
        if (preview.empty())
            preview = std::to_string(current);

        if (ImGui::BeginCombo(id.c_str(), preview.c_str()))
        {
            for (const EnumOption& option : field.Enum)
            {
                const bool selected = option.Value == current;
                if (ImGui::Selectable(EnumOptionLabel(option).c_str(), selected)
                    && !selected)
                {
                    WriteEnumUnderlying(ptr, field, option.Value);
                    edit.Activated = true;
                    edit.Committed = true;
                }
                if (!option.Tooltip.empty()
                    && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("%.*s",
                                      static_cast<int>(option.Tooltip.size()),
                                      option.Tooltip.data());
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return edit;
    }

    // One row's left-column text: the schema's declared label when it has
    // one, else the humanized persisted name. A declared tooltip shows on
    // hover. Display only -- ids and serialization keep the raw path.
    void DrawFieldLabel(const RuntimeField& field)
    {
        ImGui::AlignTextToFramePadding();
        if (field.Label.empty())
            ImGui::TextUnformatted(HumanizeFieldLabel(field.Name).c_str());
        else
            ImGui::TextUnformatted(field.Label.data(),
                                   field.Label.data() + field.Label.size());
        if (!field.Tooltip.empty()
            && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%.*s", static_cast<int>(field.Tooltip.size()),
                              field.Tooltip.data());
    }

    // Lays out one row as [label column | widget column], the widget filling the
    // rest of the width — the two-column inspector look. The widget uses a hidden
    // ("##") label so only the left-column text shows.
    FieldEdit DrawRuntimeField(const RuntimeField& field, std::byte* component, float labelWidth)
    {
        FieldEdit edit;
        void* ptr = component + field.Offset;

        DrawFieldLabel(field);
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-FLT_MIN);

        const std::string id = "##" + field.Name;
        if (field.Scalar == FieldScalar::Unsupported)
        {
            ImGui::TextDisabled("<unsupported>");
            return edit;
        }

        // An identity id shows its value greyed and non-interactive; a disabled
        // widget never reports activation, so it stays out of the edit/commit path.
        if (field.ReadOnly)
            ImGui::BeginDisabled();

        if (!field.Enum.empty())
        {
            edit = DrawEnumField(field, ptr, id);
            if (field.ReadOnly)
                ImGui::EndDisabled();
            return edit;
        }

        // An angle a designer thinks about in degrees, stored in radians. The
        // conversion is the widget's alone: nothing but this row ever sees
        // degrees, so the component, the scene, and the wire are unchanged.
        if (field.DisplayDegrees && field.Scalar == FieldScalar::Float)
        {
            constexpr float kRadToDeg = 180.0f / std::numbers::pi_v<float>;
            const int count = std::min<int>(field.Count, 4);
            std::array<float, 4> degrees{};
            for (int i = 0; i < count; ++i)
                degrees[static_cast<std::size_t>(i)] =
                    static_cast<const float*>(ptr)[i] * kRadToDeg;

            ImGui::DragScalarN(id.c_str(), ImGuiDataType_Float, degrees.data(), count,
                               0.5f, nullptr, nullptr, "%.1f\xc2\xb0");
            if (ImGui::IsItemEdited())
                for (int i = 0; i < count; ++i)
                    static_cast<float*>(ptr)[i] =
                        degrees[static_cast<std::size_t>(i)] / kRadToDeg;

            edit.Activated = ImGui::IsItemActivated();
            edit.Committed = ImGui::IsItemDeactivatedAfterEdit();
            if (field.ReadOnly)
                ImGui::EndDisabled();
            return edit;
        }

        if (field.Scalar == FieldScalar::Bool)
            ImGui::Checkbox(id.c_str(), reinterpret_cast<bool*>(ptr));
        else if (field.Scalar == FieldScalar::Color3)
            ImGui::ColorEdit3(id.c_str(), reinterpret_cast<float*>(ptr),
                              ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        else
            ImGui::DragScalarN(id.c_str(), DataTypeFor(field), ptr,
                               static_cast<int>(field.Count), 0.05f);

        edit.Activated = ImGui::IsItemActivated();
        edit.Committed = ImGui::IsItemDeactivatedAfterEdit();

        if (field.ReadOnly)
            ImGui::EndDisabled();
        return edit;
    }
}

InspectorPanel::InspectorPanel(WorldDocument& world,
                               SelectionService& selection,
                               CommandStack& commands,
                               EditorComponentAdapterRegistry& adapters)
    : WorldDoc(world)
    , Selection(selection)
    , Commands(commands)
    , Adapters(adapters)
{
}

std::string_view InspectorPanel::GetTitle() const
{
    return "Inspector";
}

void InspectorPanel::ResetEditState()
{
    // An edit interrupted before its commit (selection change, panel switch) has
    // already written this frame's preview bytes into the live component. Restore
    // the pre-edit snapshot so an abandoned edit cannot strand an un-undoable
    // mutation.
    if (EditActive && EditingComponent != InvalidComponentId && !EditBefore.empty())
    {
        World& world = WorldDoc.FocusDocument().GetScene().GetRegistry().Components;
        if (void* live = world.GetComponentRaw(EditingEntity, EditingComponent))
            std::memcpy(live, EditBefore.data(), EditBefore.size());
    }
    EditActive = false;
    EditingEntity = {};
    EditingComponent = InvalidComponentId;
    EditBefore.clear();
}

const InspectorPanel::BaselineEntry& InspectorPanel::BaselineFor(
    IComponentSerializer& serializer, EntityId entity, ComponentId component)
{
    if (BaselineEntity != entity)
    {
        BaselineCache.clear();
        BaselineEntity = entity;
    }
    const auto found = BaselineCache.find(component);
    if (found != BaselineCache.end())
        return found->second;

    BaselineEntry entry;
    entry.Bytes =
        WorldDoc.FocusDocument().BaselineComponentBytes(entity, serializer);
    entry.Present = !entry.Bytes.empty();
    return BaselineCache.emplace(component, std::move(entry)).first->second;
}

void InspectorPanel::DrawComponent(IComponentSerializer& serializer, EntityId entity)
{
    World& world = WorldDoc.FocusDocument().GetScene().GetRegistry().Components;
    const ComponentId id = world.GetComponentIdByType(serializer.TypeId());
    if (id == InvalidComponentId)
        return;

    const ComponentMeta* meta = world.GetMeta(id);
    const std::size_t size = meta ? meta->Size : 0;

    const std::string key(serializer.JsonKey());

    // Override state, for members of a scene instance: the baseline is what
    // the placement's source says, and a field whose bytes differ from it is
    // an override. Asset-handle and read-only fields stay out of the byte
    // comparison -- handles are session-local values that differ even when
    // they name the same asset.
    EditorDocument& document = WorldDoc.FocusDocument();
    const bool member = document.IsSceneInstanceMember(entity);
    const BaselineEntry* baseline =
        member ? &BaselineFor(serializer, entity, id) : nullptr;
    const std::byte* baselineBytes =
        baseline != nullptr && baseline->Present
                && baseline->Bytes.size() == size
            ? baseline->Bytes.data()
            : nullptr;
    // After BaselineFor on purpose: a cache miss there creates and destroys a
    // scratch entity, and structural change can relocate this pointer. Const
    // access, so inspecting a component cannot mark its chunk changed.
    const void* liveRaw =
        size > 0 ? std::as_const(world).GetComponentRaw(entity, id) : nullptr;
    const auto fieldOverridden = [&](const RuntimeField& field)
    {
        if (baselineBytes == nullptr || liveRaw == nullptr
            || field.Asset != AssetType::Unknown || field.ReadOnly)
        {
            return false;
        }
        const std::size_t span = field.Size * field.Count;
        if (field.Offset + span > size)
            return false;
        return std::memcmp(static_cast<const std::byte*>(liveRaw) + field.Offset,
                           baselineBytes + field.Offset, span) != 0;
    };
    bool componentOverridden = member && baseline != nullptr && !baseline->Present;
    // Whether a byte-level reset is even safe: RawComponentEditCommand is a
    // blind memcpy, which asset-handle fields (refcounted, session-local)
    // must never travel through. The per-field verdicts feed the header here
    // and the row badges below, so each field is compared exactly once.
    bool holdsAssetFields = false;
    const std::span<const RuntimeField> fields = serializer.RuntimeFields();
    FieldOverrideScratch.assign(fields.size(), 0);
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        holdsAssetFields |= fields[i].Asset != AssetType::Unknown;
        FieldOverrideScratch[i] = fieldOverridden(fields[i]) ? 1 : 0;
        componentOverridden |= FieldOverrideScratch[i] != 0;
    }

    // Stable id from the JsonKey; the icon is display-only (kept out of the id).
    const std::string header = std::string(ICON_FA_CUBES "  ") + key + "###" + key;
    // Let the trash button below sit on top of the full-width header and take its
    // own clicks (otherwise the header swallows them as a collapse toggle).
    ImGui::SetNextItemAllowOverlap();
    const bool open = ImGui::CollapsingHeader(header.c_str());

    // Header affordances: a right-click context menu (remove, and reset for an
    // overridden member component) and a right-aligned trash button. Removal
    // defers to PendingRemoval, executed after the loop; suppressed for
    // components the registry marks non-removable (the transform).
    const bool resettable = componentOverridden && baselineBytes != nullptr
        && !holdsAssetFields;
    if (serializer.IsRemovable() || resettable)
    {
        if (ImGui::BeginPopupContextItem(("##ctx_" + key).c_str()))
        {
            if (serializer.IsRemovable()
                && ImGui::MenuItem(ICON_FA_TRASH "  Remove Component"))
                PendingRemoval = &serializer;
            if (resettable
                && ImGui::MenuItem(ICON_FA_ROTATE_LEFT "  Reset to Source"))
            {
                std::vector<std::byte> current(size);
                std::memcpy(current.data(), liveRaw, size);
                Commands.Execute(std::make_unique<RawComponentEditCommand>(
                    entity, id, std::move(current),
                    std::vector<std::byte>(baselineBytes, baselineBytes + size),
                    document.GetScene(), document));
            }
            ImGui::EndPopup();
        }
    }
    if (serializer.IsRemovable())
    {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
        if (ImGui::SmallButton((std::string(ICON_FA_TRASH) + "##del_" + key).c_str()))
            PendingRemoval = &serializer;
    }
    // The override badge, in the header's right gutter beside the trash spot:
    // a draw-list dot, so the layout never shifts as overrides come and go.
    if (componentOverridden)
    {
        const ImVec2 rectMin = ImGui::GetItemRectMin();
        const ImVec2 rectMax = ImGui::GetItemRectMax();
        const float inset = ImGui::GetFrameHeight() * 1.8f;
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(ImGui::GetContentRegionMax().x + ImGui::GetWindowPos().x
                       - inset,
                   (rectMin.y + rectMax.y) * 0.5f),
            3.0f, ImGui::GetColorU32(EditorUi::Accent));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(baseline != nullptr && !baseline->Present
                                  ? "Added to this instance; the source has no "
                                    "such component."
                                  : "Overrides the placement's source. "
                                    "Right-click to reset.");
    }

    if (!open)
        return;

    ImGui::PushID(key.c_str());

    if (const IEditorComponentAdapter* adapter = Adapters.Find(serializer.TypeId());
        adapter != nullptr)
    {
        EditorComponentInspectorContext context{
                .World = WorldDoc,
                .Document = document,
                .Scene = document.GetScene(),
                .Selection = Selection,
                .Commands = Commands,
                .Entity = entity,
            };
        if (adapter->DrawInspector(context))
        {
            ImGui::PopID();
            return;
        }
    }

    // Work on a copy of the component's bytes so reads don't churn change
    // tracking every frame; write back only when a widget actually edits.
    std::vector<std::byte> working(size);
    if (size > 0)
    {
        const void* live = std::as_const(world).GetComponentRaw(entity, id);
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
    for (const RuntimeField& field : fields)
    {
        // Asset-handle fields resolve through the asset system, not raw scalar
        // bytes: the picker reads/writes the live component directly via its own
        // undoable command, so it sits outside the working-copy scalar path.
        if (field.Asset != AssetType::Unknown)
        {
            DrawAssetField(field, entity, id, labelWidth);
            continue;
        }
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const FieldEdit edit = DrawRuntimeField(field, working.data(), labelWidth);
        activated |= edit.Activated;
        committed |= edit.Committed;

        if (FieldOverrideScratch[&field - fields.data()] == 0)
            continue;
        // The field's override badge sits in the label gutter, and the row's
        // last widget carries the reset in its context menu.
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(rowMin.x - 6.0f,
                   rowMin.y + ImGui::GetFrameHeight() * 0.5f),
            2.5f, ImGui::GetColorU32(EditorUi::Accent));
        if (baselineBytes != nullptr
            && ImGui::BeginPopupContextItem(
                (std::string("##fieldctx_") + field.Name).c_str()))
        {
            if (ImGui::MenuItem(ICON_FA_ROTATE_LEFT "  Reset Field to Source"))
            {
                std::vector<std::byte> current(size);
                std::memcpy(current.data(), liveRaw, size);
                std::vector<std::byte> reset = current;
                const std::size_t span = field.Size * field.Count;
                std::memcpy(reset.data() + field.Offset,
                            baselineBytes + field.Offset, span);
                Commands.Execute(std::make_unique<RawComponentEditCommand>(
                    entity, id, std::move(current), std::move(reset),
                    document.GetScene(), document));
            }
            ImGui::EndPopup();
        }
    }

    // Begin an undoable edit: snapshot the pre-edit bytes on widget activation.
    // Refresh on every activation (only one widget is active at a time) so a
    // click-release without an edit can't leave a stale snapshot behind.
    if (activated)
    {
        EditActive = true;
        EditingEntity = entity;
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
            entity, id, EditBefore, working, WorldDoc.FocusDocument().GetScene(), WorldDoc.FocusDocument()));
        EditActive = false;
        EditingComponent = InvalidComponentId;
    }

    ImGui::PopID();
}

// Stable, sorted assets of one kind (AssetRegistry::Records() is unordered),
// narrowed to the subtype a structured-data field accepts. A field that names
// no subtype takes any data asset, and a field of any other kind has nothing
// to narrow by.
std::vector<InspectorPanel::AssetPickerEntry> InspectorPanel::PickerCandidates(
    const AssetRegistry& catalog, AssetSystem& assets, const RuntimeField& field)
{
    std::vector<AssetPickerEntry> entries;
    for (const auto& entry : catalog.Records())
        if (entry.second.Type == field.Asset)
            entries.push_back({ entry.first, entry.second.Id });
    std::sort(entries.begin(), entries.end(),
              [](const AssetPickerEntry& a, const AssetPickerEntry& b)
              { return a.Path < b.Path; });

    if (field.Asset != AssetType::Data || field.DataSubtype.empty())
        return entries;

    std::erase_if(entries, [&](const AssetPickerEntry& entry)
    {
        const AssetRecord* record = catalog.FindByPath(entry.Path);
        return record == nullptr
            || PeekDataAssetSubtype(assets.DefaultSource(), *record)
                   != field.DataSubtype;
    });
    return entries;
}

bool InspectorPanel::DrawAssetPickCombo(const char* widgetId,
                                        const AssetFieldRef& current,
                                        const AssetRegistry& catalog,
                                        AssetSystem& assets,
                                        const RuntimeField& field,
                                        AssetFieldRef& picked)
{
    bool changed = false;
    const std::uint32_t pickerId = ImGui::GetID(widgetId);
    const char* preview = current.Path.empty() ? "(none)" : current.Path.c_str();
    if (ImGui::BeginCombo(widgetId, preview))
    {
        if (OpenPicker != pickerId || ImGui::IsWindowAppearing())
        {
            OpenPicker = pickerId;
            OpenPickerEntries = PickerCandidates(catalog, assets, field);
        }
        if (ImGui::Selectable("(none)", current.Path.empty()) && !current.Path.empty())
        {
            picked = AssetFieldRef{};
            changed = true;
        }
        if (OpenPickerEntries.empty())
            ImGui::TextDisabled("%s", field.DataSubtype.empty()
                                          ? "No assets of this kind in the project."
                                          : "No matching assets in the project.");
        for (const AssetPickerEntry& entry : OpenPickerEntries)
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

void InspectorPanel::DrawAssetField(const RuntimeField& field, EntityId entity,
                                    ComponentId component, float labelWidth)
{
    AssetSystem* assets = WorldDoc.FocusDocument().GetAssetSystem();
    const AssetRegistry* catalog = WorldDoc.FocusDocument().GetAssetCatalog();
    if (assets == nullptr || catalog == nullptr)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(HumanizeFieldLabel(field.Name).c_str());
        ImGui::SameLine(labelWidth);
        ImGui::TextDisabled("<no asset system>");
        return;
    }

    World& world = WorldDoc.FocusDocument().GetScene().GetRegistry().Components;
    const void* base = std::as_const(world).GetComponentRaw(entity, component);
    if (base == nullptr)
        return;
    const void* fieldPtr = static_cast<const std::byte*>(base) + field.Offset;
    const AssetFieldValue current = ReadAssetField(*assets, field.Asset, field.Arity, fieldPtr);

    // One edit = one full before/after value through the refcount-balanced command.
    const auto apply = [&](AssetFieldValue next) {
        Commands.Execute(std::make_unique<AssetFieldEditCommand>(
            entity, component, field.Offset, field.Asset, field.Arity,
            current, std::move(next), WorldDoc.FocusDocument().GetScene(), WorldDoc.FocusDocument(), *assets));
    };

    if (field.Arity != AssetArity::List)
    {
        DrawFieldLabel(field);
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-FLT_MIN);

        const AssetFieldRef cur = current.Refs.empty() ? AssetFieldRef{} : current.Refs.front();
        AssetFieldRef picked;
        if (DrawAssetPickCombo(("##" + field.Name).c_str(), cur, *catalog, *assets,
                               field, picked))
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
    DrawFieldLabel(field);

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
        if (DrawAssetPickCombo("##slot", current.Refs[i], *catalog, *assets, field,
                               picked))
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
    World& world = WorldDoc.FocusDocument().GetScene().GetRegistry().Components;

    // OpenPopup only sets state; BeginPopup must run every frame or ImGui closes
    // the popup before a selection can be made.
    if (ImGui::Button(ICON_FA_PLUS "  Add Component"))
        ImGui::OpenPopup("##add_component");

    if (ImGui::BeginPopup("##add_component"))
    {
        bool anyAddable = false;
        for (const auto& serializer : EditorSceneSerializers().Entries())
        {
            // A component the editor may not remove is one the document owns, so
            // hand-adding it would produce an entity whose managed state the
            // owner never minted (an unset persistent id, a transform with no
            // derived pair).
            if (!serializer->IsRemovable())
                continue;
            const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
            if (id == InvalidComponentId || world.HasComponent(entity, id))
                continue;

            anyAddable = true;
            const std::string label(serializer->JsonKey());
            if (ImGui::Selectable(label.c_str()))
            {
                Commands.Execute(std::make_unique<RawComponentAddCommand>(
                    entity, id, serializer->DefaultBytes(), WorldDoc.FocusDocument().GetScene(), WorldDoc.FocusDocument()));
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

    if (!selection.IsValid() || !WorldDoc.FocusDocument().GetScene().HasEntity(entity))
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
    World& world = WorldDoc.FocusDocument().GetScene().GetRegistry().Components;
    for (const auto& serializer : EditorSceneSerializers().Entries())
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
            entity, *PendingRemoval, WorldDoc.FocusDocument().GetScene(), WorldDoc.FocusDocument()));
        PendingRemoval = nullptr;
        ResetEditState();
    }

    DrawDerivedComponents(entity);

    ImGui::Separator();
    DrawAddComponentMenu(entity);
}

void InspectorPanel::DrawDerivedComponents(EntityId entity)
{
    const World& world =
        std::as_const(WorldDoc.FocusDocument().GetScene().GetRegistry()).Components;
    const std::vector<DerivedComponentRow> rows =
        DerivedComponentsOn(world, EditorSceneSerializers(), entity);
    if (rows.empty())
        return;

    // Recomputed rather than cached: one signature walk plus a lookup per
    // component, for one selected entity, against a loop above that already
    // builds a string and a byte copy per component per frame.
    ImGui::PushStyleColor(ImGuiCol_Text, EditorUi::TextDim);
    const std::string header = "Derived Components (" + std::to_string(rows.size())
        + ")###derived_components";
    const bool open = ImGui::CollapsingHeader(header.c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip(
            "On the entity, not in the scene file. Systems keep these; a "
            "component that cannot work without them brings them along. "
            "Nothing here is authored, and nothing here is saved.");
    }
    if (!open)
        return;

    ImGui::Indent();
    for (const DerivedComponentRow& row : rows)
    {
        const std::string label = HumanizeFieldLabel(std::string(row.Name));
        ImGui::BulletText("%s", label.c_str());
        // The stable name on hover rather than in the row: it is a wire key,
        // and it is what a search of the source will actually find.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%.*s", static_cast<int>(row.Name.size()), row.Name.data());

        const ComponentMeta* owner = row.ProvidedBy == InvalidComponentId
            ? nullptr
            : world.GetMeta(row.ProvidedBy);
        if (owner == nullptr)
            continue;

        ImGui::SameLine();
        ImGui::TextColored(EditorUi::TextDim, "from %s",
                           HumanizeFieldLabel(std::string(owner->Name)).c_str());
    }
    ImGui::Unindent();
}
