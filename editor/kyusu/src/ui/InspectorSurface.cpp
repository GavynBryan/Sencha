#include "InspectorSurface.h"

#include "RuntimeFieldText.h"

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/WorldDocument.h"
#include "document/commands/RawComponentEditCommand.h"
#include "selection/SelectionService.h"

#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/IComponentSerializer.h>

#include <cstring>
#include <memory>
#include <utility>

namespace
{
constexpr UiModelRowsId kFields = UiModelRowsId{ 1 };

constexpr UiModelPropertyId kEntityLabel = UiModelPropertyId{ 1 };
constexpr UiModelPropertyId kStatus = UiModelPropertyId{ 2 };
constexpr UiModelPropertyId kHasSelection = UiModelPropertyId{ 3 };

constexpr UiActionId kBegin = UiActionId{ 1 };
constexpr UiActionId kCommit = UiActionId{ 2 };
constexpr UiActionId kCloseAction = UiActionId{ 3 };

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
    desc.Properties = {
        UiModelProperty{ "entity_label", UiValue(std::string{}) },
        UiModelProperty{ "status", UiValue(std::string{}) },
        UiModelProperty{ "has_selection", UiValue(false) },
    };
    desc.Actions = { "inspector_begin", "inspector_commit", "inspector_close" };
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
                origins.push_back(RowOrigin{ serializer.get(), id, 0, row.Value });
                rows.push_back(std::move(row));
                continue;
            }
            for (std::size_t i = 0; i < fields.size(); ++i)
            {
                const RuntimeField& field = fields[i];
                UiRow row;
                row.Label = RuntimeFieldLabel(field);
                // Which component the field belongs to, on every row rather than
                // once per group: a row that says what it is stays meaningful
                // when the list is filtered or reordered, and grouping is then a
                // question for the stylesheet.
                row.Detail = key;
                row.Editable = IsRuntimeFieldEditable(field);
                if (row.Editable || field.ReadOnly)
                    row.Value = FormatRuntimeField(field, bytes);
                else if (field.Asset != AssetType::Unknown)
                    row.Value = "(asset)"; // refcounted; needs a picker, not text
                else
                    row.Value = "(unsupported)";

                origins.push_back(RowOrigin{ serializer.get(), id, i, row.Value });
                rows.push_back(std::move(row));
            }
        }
    }

    Origins = std::move(origins);
    (void)Ui.SetRows(Screen, kFields, rows);
}
