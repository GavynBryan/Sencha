#include "InspectorSurface.h"

#include "authoring/AssetFieldCandidates.h"
#include "authoring/RuntimeFieldText.h"

#include "authoring/EditorComponentAdapter.h"
#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EntityLabel.h"
#include "document/EntityNameComponent.h"
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
                                   CommandStack& commands,
                                   const EditorComponentAdapterRegistry& adapters)
    : Ui(ui)
    , WorldDoc(world)
    , Properties(world, selection, commands, adapters)
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
    Session.Cancel();
    // The picker goes too: it is holding a row index into a list that is about
    // to describe a different entity.
    ClosePicker();
}

void InspectorSurface::ClosePicker()
{
    PickingRow = kNoRow;
    PickerCandidates.clear();
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
    if (row >= Origins.size() || Origins[row].Key.empty())
    {
        EditingRow = kNoRow;
        return;
    }
    // Whatever the last commit had to say about a different field is no longer
    // what the person is looking at.
    Status.clear();
    EditingRow = row;
    (void)Session.Begin(Properties, Origins[row].Key);
}

void InspectorSurface::HandleCommit(std::size_t row)
{
    const bool wasEditing = EditingRow == row;
    if (wasEditing)
        EditingRow = kNoRow;

    if (row >= Origins.size() || Origins[row].Key.empty())
    {
        Session.Cancel();
        return;
    }
    const RowOrigin& origin = Origins[row];
    const std::vector<UiRow> presented = Ui.GetRows(Screen, kFields);
    if (row >= presented.size())
    {
        Session.Cancel();
        return;
    }

    const std::string& text = presented[row].Value;
    // A field somebody focused and left alone is not an edit. Compared against
    // the text this published rather than against the component, because the
    // display rounds and re-parsing what it shows would otherwise write a
    // slightly different number every time a field was merely clicked.
    if (text == origin.Published)
    {
        Session.Cancel();
        return;
    }

    const PropertyEditResult result = Session.IsOpen() && Session.Key() == origin.Key
        ? Session.Commit(text)
        : Session.Set(Properties, origin.Key, text);
    Report(result, origin, text);
}

void InspectorSurface::Report(PropertyEditResult result, const RowOrigin& origin, const std::string& text)
{
    // Refused, and said so. The next publish refills the row from the
    // component, which is what puts the old text back on screen.
    if (result == PropertyEditResult::Refused)
        Status = "\"" + text + "\" is not a value " + origin.Label + " can hold";
    else if (result == PropertyEditResult::Applied)
        Status = origin.Label + " updated";
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


void InspectorSurface::HandlePick(std::size_t row)
{
    ClosePicker();
    Status.clear();
    if (row >= Origins.size() || !Origins[row].Pickable)
        return; // a read-only row with nothing to choose from

    if (WorldDoc.FocusDocument().GetAssetSystem() == nullptr
        || WorldDoc.FocusDocument().GetAssetCatalog() == nullptr)
    {
        Status = "this document has no asset system to pick from";
        return;
    }
    // Scanned once, here. Narrowing a structured-data field reads each
    // candidate's envelope off disk, which is not work to repeat on every frame
    // a picker happens to be open.
    PickerCandidates = Properties.AssetChoices(Origins[row].Key);
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

    // Index zero is "(none)", so the candidates start at one. An index past the
    // end is a document and a host disagreeing about a list that changed under
    // them, which is a refusal rather than a guess.
    std::string path;
    if (option > 0)
    {
        if (option - 1 >= candidates.size())
            return;
        path = candidates[option - 1].Path;
    }
    const RowOrigin origin = Origins[row];
    Report(Session.Set(Properties, origin.Key, path), origin, path);
}

std::string InspectorSurface::EntityLabel(EntityId entity) const
{
    if (!entity.IsValid())
        return "No selection";
    return ::EntityLabel(std::as_const(WorldDoc.FocusDocument().GetScene().GetRegistry()).Components, entity);
}

void InspectorSurface::Publish()
{
    const EntityId entity = Properties.Entity();
    if (!(entity == Showing))
    {
        // The entity the rows described is gone or replaced, so the row indices
        // the document is holding no longer mean anything.
        AbandonEdit();
        Showing = entity;
        Status.clear();
    }

    (void)Ui.SetValue(Screen, kHasSelection, UiValue(entity.IsValid()));
    (void)Ui.SetValue(Screen, kEntityLabel, UiValue(EntityLabel(entity)));
    (void)Ui.SetValue(Screen, kStatus, UiValue(Status));
    PublishPicker();

    // Refilling a row that is being typed into would delete the keystrokes
    // between one frame and the next, so the whole list holds still until the
    // document says the edit is finished.
    if (EditingRow != kNoRow)
        return;

    std::vector<UiRow> rows;
    std::vector<RowOrigin> origins;
    const auto emit = [&](UiRow row, RowOrigin origin) {
        origin.Published = row.Value;
        origins.push_back(std::move(origin));
        rows.push_back(std::move(row));
    };

    // Which component a row belongs to is on every row rather than once per
    // group: a row that says what it is stays meaningful when the list is
    // filtered or reordered, and grouping is then a question for the stylesheet.
    for (const PropertyNode& group : Properties.Nodes())
    {
        if (group.Descriptor.PresentationHint == kComponentAdapterHint)
        {
            emit(UiRow{ group.Descriptor.Label, "(edited in the panel)", group.Key }, RowOrigin{});
            continue;
        }
        if (group.Children.empty())
        {
            // A tag, or anything else that describes no leaves. Given a row of
            // its own because otherwise an entity carrying it looks exactly
            // like one that does not.
            emit(UiRow{ group.Descriptor.Label, "(no fields)", group.Key }, RowOrigin{});
            continue;
        }
        for (const PropertyNode& field : group.Children)
        {
            UiRow row{ field.Descriptor.Label, field.Value, group.Key };
            RowOrigin origin{ .Key = field.Key, .Label = field.Descriptor.Label };
            if (field.Descriptor.Kind == PropertyKind::Asset)
            {
                // Never editable as text. A path typed into a box is a path that
                // does not exist yet, and the reference is an id first.
                row.Value = field.Value.empty() ? std::string(kNoAsset) : ShortAssetPath(field.Value);
                origin.Pickable = !field.Descriptor.ReadOnly;
                origin.PickerLabel = field.Descriptor.Label;
            }
            else if (field.Descriptor.Kind == PropertyKind::Unsupported)
            {
                row.Value = "(unsupported)";
            }
            else
            {
                row.Editable = !field.Descriptor.ReadOnly;
            }
            emit(std::move(row), std::move(origin));
        }
    }

    Origins = std::move(origins);
    (void)Ui.SetRows(Screen, kFields, rows);
}
