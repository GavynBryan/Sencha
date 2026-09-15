#include "InspectorSurface.h"

#include "AssetFieldCandidates.h"
#include "RuntimeFieldText.h"

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/WorldDocument.h"
#include "document/AssetFieldIo.h"
#include "document/commands/AssetFieldEditCommand.h"
#include "document/commands/RawComponentEditCommand.h"
#include "selection/SelectionService.h"

#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetRegistry.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/IComponentSerializer.h>

#include <cstring>
#include <memory>
#include <utility>

namespace
{
constexpr UiModelRowsId kFields = UiModelRowsId{ 1 };
constexpr UiModelArrayId kPickerOptions = UiModelArrayId{ 1 };

constexpr UiModelPropertyId kEntityLabel = UiModelPropertyId{ 1 };
constexpr UiModelPropertyId kStatus = UiModelPropertyId{ 2 };
constexpr UiModelPropertyId kHasSelection = UiModelPropertyId{ 3 };
constexpr UiModelPropertyId kPickerOpen = UiModelPropertyId{ 4 };
constexpr UiModelPropertyId kPickerLabel = UiModelPropertyId{ 5 };

constexpr UiActionId kBegin = UiActionId{ 1 };
constexpr UiActionId kCommit = UiActionId{ 2 };
constexpr UiActionId kCloseAction = UiActionId{ 3 };
constexpr UiActionId kPick = UiActionId{ 4 };
constexpr UiActionId kChoose = UiActionId{ 5 };
constexpr UiActionId kCancelPick = UiActionId{ 6 };

// What an unset asset field reads as, and the first entry of every picker.
constexpr std::string_view kNoAsset = "(none)";

// A reference as a row reads it. The scheme is on every asset path there is, so
// it costs eight characters of a narrow column to say nothing; what identifies
// the asset is the rest. Display only -- the reference the host holds and the
// paths the picker offers are untouched.
[[nodiscard]] std::string ShortAssetPath(const std::string& path)
{
    constexpr std::string_view kScheme = "asset://";
    return path.starts_with(kScheme) ? path.substr(kScheme.size()) : path;
}

[[nodiscard]] std::size_t RowArgument(const UiAction& action)
{
    if (action.Arguments.empty())
        return static_cast<std::size_t>(-1);
    const std::int64_t index = action.Arguments[0].AsInt();
    return index < 0 ? static_cast<std::size_t>(-1) : static_cast<std::size_t>(index);
}
} // namespace

InspectorSurface::InspectorSurface(UiService& ui,
                                   UiSurfaceId surface,
                                   WorldDocument& world,
                                   SelectionService& selection,
                                   CommandStack& commands)
    : Ui(ui)
    , WorldDoc(world)
    , Selection(selection)
    , Commands(commands)
    , Surface(surface)
{
}

UiScreenDesc InspectorSurface::Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://inspector.rml";
    desc.ModelName = "inspector";
    // Not modal. An inspector that took focus from the viewport would make the
    // editor worse than the panel it is trying to replace: picking an entity is
    // how the inspector gets anything to show.
    desc.Modal = false;

    desc.RowLists = { "fields" };
    desc.Arrays = { "picker_options" };
    desc.Properties = {
        UiModelProperty{ "entity_label", UiValue(std::string{}) },
        UiModelProperty{ "status", UiValue(std::string{}) },
        UiModelProperty{ "has_selection", UiValue(false) },
        UiModelProperty{ "picker_open", UiValue(false) },
        UiModelProperty{ "picker_label", UiValue(std::string{}) },
    };
    desc.Actions = { "inspector_begin", "inspector_commit", "inspector_close",
                     "inspector_pick", "inspector_choose", "inspector_cancel_pick" };
    return desc;
}

void InspectorSurface::Open()
{
    if (Screen.IsValid() || !Surface.IsValid())
        return;

    Screen = Ui.OpenScreen(Surface, Describe());
    if (!Screen.IsValid())
        return;

    Origins.clear();
    Showing = {};
    EditingRow = kNoRow;
    Status.clear();
    Publish();
}

void InspectorSurface::Close()
{
    if (!Screen.IsValid())
        return;
    AbandonEdit();
    Ui.CloseScreen(Screen);
    Screen = {};
    Origins.clear();
    Showing = {};
}

void InspectorSurface::AbandonEdit()
{
    // Nothing to roll back. A field's text lives in the screen's presentation
    // copy until a commit reads it back, so an edit interrupted by a selection
    // change, a close, or the entity going away simply never happened.
    EditingRow = kNoRow;
    // The picker goes too: it is holding a row index into a list that is about
    // to describe a different entity.
    ClosePicker();
}

void InspectorSurface::ClosePicker()
{
    PickingRow = kNoRow;
    PickerCandidates.clear();
}

EntityId InspectorSurface::SelectedEntity() const
{
    const SelectableRef selection = Selection.GetPrimarySelection();
    if (!selection.IsEntity())
        return {};
    if (!WorldDoc.FocusDocument().GetScene().HasEntity(selection.Entity))
        return {};
    return selection.Entity;
}

void InspectorSurface::Update()
{
    if (!Screen.IsValid())
        return;

    for (const UiAction& action : Ui.DrainActions(Screen))
    {
        if (action.Id == kBegin)
        {
            HandleBegin(RowArgument(action));
        }
        else if (action.Id == kCommit)
        {
            HandleCommit(RowArgument(action));
        }
        else if (action.Id == kPick)
        {
            HandlePick(RowArgument(action));
        }
        else if (action.Id == kChoose)
        {
            HandleChoose(RowArgument(action));
        }
        else if (action.Id == kCancelPick)
        {
            ClosePicker();
        }
        else if (action.Id == kCloseAction)
        {
            Close();
            return;
        }
    }

    Publish();
}

void InspectorSurface::HandleBegin(std::size_t row)
{
    if (row >= Origins.size())
    {
        EditingRow = kNoRow;
        return;
    }
    // Whatever the last commit had to say about a different field is no longer
    // what the person is looking at.
    Status.clear();
    EditingRow = row;
}

void InspectorSurface::HandleCommit(std::size_t row)
{
    const bool wasEditing = EditingRow == row;
    if (wasEditing)
        EditingRow = kNoRow;

    if (row >= Origins.size())
        return;
    const RowOrigin& origin = Origins[row];
    if (origin.Serializer == nullptr)
        return;

    const std::vector<UiRow> presented = Ui.GetRows(Screen, kFields);
    if (row >= presented.size())
        return;

    const std::string& text = presented[row].Value;
    // A field somebody focused and left alone is not an edit. Compared against
    // the text this published rather than against the component, because the
    // display rounds and re-parsing what it shows would otherwise write a
    // slightly different number every time a field was merely clicked.
    if (text == origin.Published)
        return;

    const EntityId entity = SelectedEntity();
    if (!entity.IsValid())
        return;

    EditorDocument& document = WorldDoc.FocusDocument();
    World& world = document.GetScene().GetRegistry().Components;
    const ComponentMeta* meta = world.GetMeta(origin.Component);
    const void* live = std::as_const(world).GetComponentRaw(entity, origin.Component);
    if (meta == nullptr || live == nullptr || meta->Size == 0)
        return;

    const std::span<const RuntimeField> fields = origin.Serializer->RuntimeFields();
    if (origin.Field >= fields.size())
        return;
    const RuntimeField& field = fields[origin.Field];

    std::vector<std::byte> before(meta->Size);
    std::memcpy(before.data(), live, meta->Size);
    std::vector<std::byte> after = before;

    if (!ParseRuntimeField(field, text, after.data()))
    {
        // Refused, and said so. The next publish refills the row from the
        // component, which is what puts the old text back on screen.
        Status = "\"" + text + "\" is not a value " + RuntimeFieldLabel(field)
            + " can hold";
        return;
    }
    if (after == before)
        return;

    Commands.Execute(std::make_unique<RawComponentEditCommand>(
        entity, origin.Component, std::move(before), std::move(after),
        document.GetScene(), document));
    Status = RuntimeFieldLabel(field) + " updated";
}

void InspectorSurface::PublishPicker()
{
    const bool open = PickingRow != kNoRow;
    (void)Ui.SetValue(Screen, kPickerOpen, UiValue(open));
    if (!open)
    {
        (void)Ui.SetArray(Screen, kPickerOptions, {});
        (void)Ui.SetValue(Screen, kPickerLabel, UiValue(std::string{}));
        return;
    }

    std::vector<std::string> options;
    options.reserve(PickerCandidates.size() + 1);
    // Clearing the field is a choice like any other, so it is the first entry
    // rather than a separate affordance the document would have to know about.
    options.emplace_back(kNoAsset);
    for (const AssetFieldCandidate& candidate : PickerCandidates)
        options.push_back(candidate.Path);
    (void)Ui.SetArray(Screen, kPickerOptions, options);

    const std::string label = PickingRow < Origins.size()
        ? Origins[PickingRow].PickerLabel
        : std::string{};
    (void)Ui.SetValue(Screen, kPickerLabel, UiValue(label));
}

void InspectorSurface::AppendAssetRows(const IComponentSerializer& serializer,
                                       ComponentId component,
                                       std::size_t fieldIndex,
                                       const RuntimeField& field,
                                       const void* bytes,
                                       const std::string& componentKey,
                                       std::vector<UiRow>& rows,
                                       std::vector<RowOrigin>& origins) const
{
    const std::string label = RuntimeFieldLabel(field);

    AssetSystem* assets = WorldDoc.FocusDocument().GetAssetSystem();
    if (assets == nullptr || bytes == nullptr)
    {
        UiRow row;
        row.Label = label;
        row.Detail = componentKey;
        row.Value = "(no asset system)";
        origins.push_back(RowOrigin{ .Serializer = &serializer,
                                     .Component = component,
                                     .Field = fieldIndex,
                                     .Published = row.Value,
                                     .PickerLabel = label });
        rows.push_back(std::move(row));
        return;
    }

    const void* fieldPtr = static_cast<const std::byte*>(bytes) + field.Offset;
    const AssetFieldValue value =
        ReadAssetField(*assets, field.Asset, field.Arity, fieldPtr);

    const auto emit = [&](std::string rowLabel, std::size_t slot, const AssetFieldRef& ref) {
        UiRow row;
        row.Label = std::move(rowLabel);
        row.Detail = componentKey;
        // Never editable as text. A path typed into a box is a path that does
        // not exist yet, and the reference is an id first.
        row.Editable = false;
        row.Value = ref.Path.empty() ? std::string(kNoAsset) : ShortAssetPath(ref.Path);
        origins.push_back(RowOrigin{ .Serializer = &serializer,
                                     .Component = component,
                                     .Field = fieldIndex,
                                     .Slot = slot,
                                     .Pickable = true,
                                     .Published = row.Value,
                                     .PickerLabel = row.Label });
        rows.push_back(std::move(row));
    };

    if (field.Arity != AssetArity::List)
    {
        emit(label, 0, value.Refs.empty() ? AssetFieldRef{} : value.Refs.front());
        return;
    }

    // A slot per authored entry, in order: this is how a mesh's per-slot
    // materials read. Adding and removing slots is not offered here, so a field
    // with none shows as one rather than vanishing.
    if (value.Refs.empty())
    {
        emit(label, 0, AssetFieldRef{});
        return;
    }
    for (std::size_t slot = 0; slot < value.Refs.size(); ++slot)
        emit(label + " " + std::to_string(slot), slot, value.Refs[slot]);
}

void InspectorSurface::HandlePick(std::size_t row)
{
    ClosePicker();
    Status.clear();
    if (row >= Origins.size() || !Origins[row].Pickable)
        return; // a read-only row with nothing to choose from

    EditorDocument& document = WorldDoc.FocusDocument();
    AssetSystem* assets = document.GetAssetSystem();
    const AssetRegistry* catalog = document.GetAssetCatalog();
    if (assets == nullptr || catalog == nullptr)
    {
        Status = "this document has no asset system to pick from";
        return;
    }

    const RowOrigin& origin = Origins[row];
    const std::span<const RuntimeField> fields = origin.Serializer->RuntimeFields();
    if (origin.Field >= fields.size())
        return;

    // Scanned once, here. Narrowing a structured-data field reads each
    // candidate's envelope off disk, which is not work to repeat on every frame
    // a picker happens to be open.
    PickerCandidates = FindAssetFieldCandidates(*catalog, *assets, fields[origin.Field]);
    PickingRow = row;
}

void InspectorSurface::HandleChoose(std::size_t option)
{
    // Taken before the picker is dismissed, because dismissing it is what drops
    // the candidate list -- and a choice dismisses it whatever happens next.
    const std::size_t row = PickingRow;
    const std::vector<AssetFieldCandidate> candidates = std::move(PickerCandidates);
    ClosePicker();
    if (row >= Origins.size())
        return;

    const RowOrigin& origin = Origins[row];
    const EntityId entity = SelectedEntity();
    if (!entity.IsValid())
        return;

    EditorDocument& document = WorldDoc.FocusDocument();
    AssetSystem* assets = document.GetAssetSystem();
    if (assets == nullptr)
        return;

    World& world = document.GetScene().GetRegistry().Components;
    const void* base = std::as_const(world).GetComponentRaw(entity, origin.Component);
    if (base == nullptr)
        return;

    const std::span<const RuntimeField> fields = origin.Serializer->RuntimeFields();
    if (origin.Field >= fields.size())
        return;
    const RuntimeField& field = fields[origin.Field];

    const void* fieldPtr = static_cast<const std::byte*>(base) + field.Offset;
    const AssetFieldValue current =
        ReadAssetField(*assets, field.Asset, field.Arity, fieldPtr);

    // Index zero is "(none)", so the candidates start at one. An index past the
    // end is a document and a host disagreeing about a list that changed under
    // them, which is a refusal rather than a guess.
    AssetFieldRef picked;
    if (option > 0)
    {
        if (option - 1 >= candidates.size())
            return;
        const AssetFieldCandidate& chosen = candidates[option - 1];
        picked = AssetFieldRef{ chosen.Id, chosen.Path };
    }

    // One edit is one whole before/after value through the refcount-balanced
    // command: a handle that is retained and released in halves does not come
    // back from an undo.
    AssetFieldValue next = current;
    if (field.Arity == AssetArity::List)
    {
        // A list with no slots still shows one row, so that a field nobody has
        // filled in yet can be filled in. Choosing there appends the slot;
        // choosing "(none)" on it asks for nothing and gets nothing.
        if (origin.Slot == next.Refs.size())
        {
            if (picked.Path.empty())
                return;
            next.Refs.push_back(std::move(picked));
        }
        else if (origin.Slot < next.Refs.size())
        {
            next.Refs[origin.Slot] = std::move(picked);
        }
        else
        {
            return;
        }
    }
    else
    {
        next.Refs.clear();
        if (!picked.Path.empty())
            next.Refs.push_back(std::move(picked));
    }

    Commands.Execute(std::make_unique<AssetFieldEditCommand>(
        entity, origin.Component, field.Offset, field.Asset, field.Arity,
        current, std::move(next), document.GetScene(), document, *assets));
    Status = RuntimeFieldLabel(field) + " updated";
}

void InspectorSurface::Publish()
{
    const EntityId entity = SelectedEntity();
    if (!(entity == Showing))
    {
        // The entity the rows described is gone or replaced, so the row indices
        // the document is holding no longer mean anything.
        AbandonEdit();
        Showing = entity;
        Status.clear();
    }

    (void)Ui.SetValue(Screen, kHasSelection, UiValue(entity.IsValid()));
    (void)Ui.SetValue(Screen, kEntityLabel,
                      UiValue(entity.IsValid()
                                  ? "Entity " + std::to_string(entity.Index)
                                        + " (gen " + std::to_string(entity.Generation) + ")"
                                  : std::string("No selection")));
    (void)Ui.SetValue(Screen, kStatus, UiValue(Status));
    PublishPicker();

    // Refilling a row that is being typed into would delete the keystrokes
    // between one frame and the next, so the whole list holds still until the
    // document says the edit is finished.
    if (EditingRow != kNoRow)
        return;

    std::vector<UiRow> rows;
    std::vector<RowOrigin> origins;

    if (entity.IsValid())
    {
        const World& world =
            std::as_const(WorldDoc.FocusDocument().GetScene().GetRegistry()).Components;

        // Registry-driven: every component the registry knows about, described
        // by its own schema. No component is named here.
        for (const auto& serializer : EditorSceneSerializers().Entries())
        {
            const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
            if (id == InvalidComponentId || !world.HasComponent(entity, id))
                continue;
            const std::string key(serializer->JsonKey());
            const std::span<const RuntimeField> fields = serializer->RuntimeFields();
            // Asked for only when there is something to read at an offset: a
            // zero-size tag has no storage, so requiring bytes first would drop
            // exactly the components the row below exists to show.
            const void* bytes =
                fields.empty() ? nullptr : world.GetComponentRaw(entity, id);
            if (!fields.empty() && bytes == nullptr)
                continue;

            if (fields.empty())
            {
                // A tag, or anything else that describes no leaves. Given a row
                // of its own because otherwise an entity carrying it looks
                // exactly like one that does not, and an entity that is quietly
                // more than its file says is the surprise.
                UiRow row;
                row.Label = HumanizeSchemaName(key);
                row.Value = "(no fields)";
                row.Detail = key;
                origins.push_back(RowOrigin{ .Serializer = serializer.get(),
                                             .Component = id,
                                             .Published = row.Value });
                rows.push_back(std::move(row));
                continue;
            }
            for (std::size_t i = 0; i < fields.size(); ++i)
            {
                const RuntimeField& field = fields[i];
                // Which component the field belongs to, on every row rather than
                // once per group: a row that says what it is stays meaningful
                // when the list is filtered or reordered, and grouping is then a
                // question for the stylesheet.
                if (field.Asset != AssetType::Unknown)
                {
                    AppendAssetRows(*serializer, id, i, field, bytes, key, rows, origins);
                    continue;
                }

                UiRow row;
                row.Label = RuntimeFieldLabel(field);
                row.Detail = key;
                row.Editable = IsRuntimeFieldEditable(field);
                row.Value = (row.Editable || field.ReadOnly)
                    ? FormatRuntimeField(field, bytes)
                    : std::string("(unsupported)");

                origins.push_back(RowOrigin{ .Serializer = serializer.get(),
                                             .Component = id,
                                             .Field = i,
                                             .Published = row.Value });
                rows.push_back(std::move(row));
            }
        }
    }

    Origins = std::move(origins);
    (void)Ui.SetRows(Screen, kFields, rows);
}
