#include "UiRuntime.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <utility>

UiRuntime::RowListDefinition::RowListDefinition(Rml::VariableDefinition& row)
    : Rml::VariableDefinition(Rml::DataVariableType::Array)
    , Row(row)
{
}

int UiRuntime::RowListDefinition::Size(void* list)
{
    return static_cast<int>(static_cast<Screen::BoundRows*>(list)->Items.size());
}

Rml::DataVariable UiRuntime::RowListDefinition::Child(void* list, const Rml::DataAddressEntry& address)
{
    Screen::BoundRows& rows = *static_cast<Screen::BoundRows*>(list);
    const std::size_t shown = rows.Items.size();
    const auto index = static_cast<std::size_t>(address.index);
    if (address.index >= 0 && index < shown)
        return Rml::DataVariable(&Row, &rows.Items[index]);
    if (address.index >= 0 && index - shown < rows.Departing.size())
        return Rml::DataVariable(&Row, &rows.Departing[index - shown]);
    if (address.name == "size")
        return Rml::MakeLiteralIntVariable(static_cast<int>(shown));
    Rml::Log::Message(Rml::Log::LT_WARNING, "Data array index out of bounds.");
    return {};
}

namespace
{
// The document engine's variant is what a binding reads and writes; UiValue is
// what crosses the public boundary. Converting at exactly these two points is
// what stops the engine's type appearing anywhere a host can see.
void ToRmlVariant(const UiValue& value, Rml::Variant& out)
{
    switch (value.Kind())
    {
    case UiValueKind::Bool:   out = Rml::Variant(value.AsBool()); break;
    case UiValueKind::Int:    out = Rml::Variant(static_cast<int>(value.AsInt())); break;
    case UiValueKind::Float:  out = Rml::Variant(static_cast<float>(value.AsFloat())); break;
    case UiValueKind::String: out = Rml::Variant(Rml::String(value.AsString())); break;
    // Presented as text: an identity is for a document to pass back, not to do
    // arithmetic on, and every id fits a string exactly where a float would
    // start losing digits above 2^53.
    case UiValueKind::Id:     out = Rml::Variant(Rml::ToString(value.AsId())); break;
    case UiValueKind::None:
    default:                  out = Rml::Variant(); break;
    }
}

UiValue FromRmlVariant(const Rml::Variant& value)
{
    switch (value.GetType())
    {
    case Rml::Variant::BOOL:   return UiValue(value.Get<bool>());
    case Rml::Variant::CHAR:
    case Rml::Variant::INT:    return UiValue(static_cast<std::int64_t>(value.Get<int>()));
    case Rml::Variant::INT64:  return UiValue(value.Get<std::int64_t>());
    case Rml::Variant::FLOAT:  return UiValue(static_cast<double>(value.Get<float>()));
    case Rml::Variant::DOUBLE: return UiValue(value.Get<double>());
    case Rml::Variant::STRING: return UiValue(value.Get<Rml::String>());
    default:                   return UiValue();
    }
}

// A data-model name the document engine can actually bind.
//
// Names are identifiers because a data expression reads a dot as member access:
// "pause.quit" is not a callback called "pause.quit", it is the member "quit"
// of something called "pause". Binding one silently fails, and the first sign
// is a button that does nothing, so it is refused here with the reason instead.
bool IsBindableName(std::string_view name)
{
    if (name.empty())
        return false;
    if (std::isdigit(static_cast<unsigned char>(name.front())) != 0)
        return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
}
} // namespace

// The types every data model on this context may bind, declared once. The
// register is the context's, not the model's, and a second declaration is a
// silent refusal that leaves a member unbound -- so this runs for the first
// model on a surface and never again for that surface.
//
// Order is load-bearing. An enum binds as an integer unless a getter is
// registered for it first; had a member of that type been registered before
// the getter, the register would have auto-inserted the integer definition and
// refused the getter. And an array's element type has to exist before the
// array, which is why the string list precedes the row and the row precedes
// the row list.
void UiRuntime::DeclareModelTypes(Rml::DataModelConstructor& constructor, Surface& surface)
{
    if (surface.ModelTypesDeclared)
        return;
    surface.ModelTypesDeclared = true;

    // Bound as its name so a document can write `row.control == 'range'`; the
    // integer it would otherwise bind as compares against nothing anyone
    // would author.
    (void)constructor.RegisterScalar<UiRowControl>(
        [](const UiRowControl& control, Rml::Variant& out) {
            out = Rml::String(UiRowControlName(control));
        });
    (void)constructor.RegisterArray<std::vector<std::string>>();
    if (auto verb = constructor.RegisterStruct<UiRowVerb>())
    {
        (void)verb.RegisterMember("key", &UiRowVerb::Key);
        (void)verb.RegisterMember("label", &UiRowVerb::Label);
        (void)verb.RegisterMember("removes", &UiRowVerb::Removes);
    }
    (void)constructor.RegisterArray<std::vector<UiRowVerb>>();

    // Members bound by pointer-to-member, which makes each one read-write.
    // That is what lets a control inside a repeated row edit its value without
    // the model needing a setter per row -- and the value still goes no further
    // than the presentation copy.
    if (auto row = constructor.RegisterStruct<UiRow>())
    {
        (void)row.RegisterMember("label", &UiRow::Label);
        (void)row.RegisterMember("value", &UiRow::Value);
        (void)row.RegisterMember("detail", &UiRow::Detail);
        (void)row.RegisterMember("editable", &UiRow::Editable);
        (void)row.RegisterMember("control", &UiRow::Control);
        (void)row.RegisterMember("number", &UiRow::Number);
        (void)row.RegisterMember("min", &UiRow::Min);
        (void)row.RegisterMember("max", &UiRow::Max);
        (void)row.RegisterMember("step", &UiRow::Step);
        (void)row.RegisterMember("choices", &UiRow::Choices);
        (void)row.RegisterMember("key", &UiRow::Key);
        (void)row.RegisterMember("depth", &UiRow::Depth);
        (void)row.RegisterMember("icon", &UiRow::Icon);
        (void)row.RegisterMember("selected", &UiRow::Selected);
        (void)row.RegisterMember("expanded", &UiRow::Expanded);
        (void)row.RegisterMember("mixed", &UiRow::Mixed);
        (void)row.RegisterMember("disabled", &UiRow::Disabled);
        (void)row.RegisterMember("parts", &UiRow::Parts);
        (void)row.RegisterMember("verbs", &UiRow::Verbs);
    }
    surface.RowLists = std::make_unique<RowListDefinition>(*constructor.GetDataTypeRegister()->GetDefinition<UiRow>());
}

bool UiRuntime::BuildModel(Screen& screen, UiScreenHandle handle, Surface& surface)
{
    Rml::DataModelConstructor constructor = surface.Context->CreateDataModel(screen.ModelName);
    if (!constructor)
    {
        Refuse(UiDiagnosticKind::ModelRefused, std::nullopt, std::format("UiRuntime: could not create data model '{}'", screen.ModelName));
        return false;
    }

    // The model is registered on the context the moment it is created, so every
    // failure below has to take it back off. Leaving it there would make the
    // next screen declaring the same model name fail to create one -- a
    // refused open poisoning the next attempt, which is far harder to read than
    // the original error.
    const auto fail = [&] {
        surface.Context->RemoveDataModel(screen.ModelName);
        return false;
    };

    for (std::size_t i = 0; i < screen.Properties.size(); ++i)
    {
        if (!IsBindableName(screen.Properties[i].Path))
        {
            Refuse(UiDiagnosticKind::ModelRefused, std::nullopt, std::format("UiRuntime: '{}' is not a bindable property name. A data expression "
                      "reads '.' as member access, so a name needs to be an identifier "
                      "(letters, digits, underscore, not starting with a digit)",
                      screen.Properties[i].Path));
            return fail();
        }

        // Bound by getter, so the value stays where the dirty comparison lives.
        // The capture is a slot position and an index, never a pointer: opening
        // a screen can move every slot, and reusing one rebuilds its properties.
        const auto slot = static_cast<std::size_t>(&screen - Screens.data());
        const std::size_t index = i;

        // A setter only where the description said a control may write. Without
        // one the binding is read-only and the document engine refuses the
        // write, rather than accepting it into a value nothing will ever read
        // back.
        Rml::DataSetFunc setter;
        if (screen.Properties[i].Editable)
        {
            setter = [this, slot, index](const Rml::Variant& in) {
                // Presentation only. Nothing here reaches the application: the
                // host reads this back when the document says to apply.
                Screens[slot].Properties[index].Value = FromRmlVariant(in);
            };
        }

        if (!constructor.BindFunc(
                screen.Properties[i].Path,
                [this, slot, index](Rml::Variant& out) { ToRmlVariant(Screens[slot].Properties[index].Value, out); },
                std::move(setter)))
        {
            Refuse(UiDiagnosticKind::ModelRefused, std::nullopt, std::format("UiRuntime: data model '{}' refused the property '{}'",
                      screen.ModelName, screen.Properties[i].Path));
            return fail();
        }
    }

    DeclareModelTypes(constructor, surface);

    for (const std::unique_ptr<Screen::BoundRows>& rows : screen.RowLists)
    {
        if (!IsBindableName(rows->Path))
        {
            Refuse(UiDiagnosticKind::ModelRefused, std::nullopt, std::format("UiRuntime: '{}' is not a bindable row-list name", rows->Path));
            return fail();
        }
        if (!constructor.BindCustomDataVariable(rows->Path, Rml::DataVariable(surface.RowLists.get(), rows.get())))
        {
            Refuse(UiDiagnosticKind::ModelRefused, std::nullopt, std::format("UiRuntime: data model '{}' refused the row list '{}'",
                      screen.ModelName, rows->Path));
            return fail();
        }
    }

    for (const std::unique_ptr<Screen::BoundArray>& array : screen.Arrays)
    {
        if (!IsBindableName(array->Path))
        {
            Refuse(UiDiagnosticKind::ModelRefused, std::nullopt, std::format("UiRuntime: '{}' is not a bindable list name, for the same reason "
                      "a property is not: a data expression reads '.' as member access",
                      array->Path));
            return fail();
        }
        if (!constructor.Bind(array->Path, &array->Items))
        {
            Refuse(UiDiagnosticKind::ModelRefused, std::nullopt, std::format("UiRuntime: data model '{}' refused the list '{}'",
                      screen.ModelName, array->Path));
            return fail();
        }
    }

    for (std::size_t i = 0; i < screen.ActionNames.size(); ++i)
    {
        if (!IsBindableName(screen.ActionNames[i]))
        {
            Refuse(UiDiagnosticKind::ModelRefused, std::nullopt, std::format("UiRuntime: '{}' is not a bindable action name, for the same reason "
                      "a property is not: a data expression reads '.' as member access",
                      screen.ActionNames[i]));
            return fail();
        }

        const UiActionId id = UiActionIdAt(i);
        if (!constructor.BindEventCallback(
                screen.ActionNames[i],
                [this, handle, id](Rml::DataModelHandle, Rml::Event&,
                                   const Rml::VariantList& arguments) {
                    UiAction action;
                    action.Screen = handle;
                    action.Id = id;
                    action.Arguments.reserve(arguments.size());
                    for (const Rml::Variant& argument : arguments)
                        action.Arguments.push_back(FromRmlVariant(argument));
                    PendingActions.push_back(std::move(action));
                }))
        {
            Refuse(UiDiagnosticKind::ModelRefused, std::nullopt, std::format("UiRuntime: data model '{}' refused the action '{}'",
                      screen.ModelName, screen.ActionNames[i]));
            return fail();
        }
    }

    screen.Model = std::make_unique<Rml::DataModelHandle>(constructor.GetModelHandle());
    return true;
}

bool UiRuntime::SetValue(UiScreenHandle screen, UiModelPropertyId property, UiValue value)
{
    Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || !property.IsValid() || property.Value > slot->Properties.size())
        return false;

    BoundProperty& bound = slot->Properties[property.Value - 1];
    // Compare before marking dirty. A model that dirties everything every frame
    // re-evaluates every binding reading it, which for a HUD publishing one
    // unchanged number is the whole cost of having a HUD.
    if (bound.Value == value)
        return false;

    bound.Value = std::move(value);
    if (slot->Model != nullptr)
        slot->Model->DirtyVariable(bound.Path);
    return true;
}

bool UiRuntime::SetArray(UiScreenHandle screen, UiModelArrayId array,
                         std::span<const std::string> items)
{
    Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || !array.IsValid() || array.Value > slot->Arrays.size())
        return false;

    Screen::BoundArray& bound = *slot->Arrays[array.Value - 1];
    // Same compare-then-dirty rule as a value. A list republished unchanged --
    // which is what a panel does every frame -- must not re-run every binding
    // that repeats over it.
    if (bound.Items.size() == items.size()
        && std::equal(bound.Items.begin(), bound.Items.end(), items.begin()))
    {
        return false;
    }

    bound.Items.assign(items.begin(), items.end());
    if (slot->Model != nullptr)
        slot->Model->DirtyVariable(bound.Path);
    return true;
}

bool UiRuntime::SetRows(UiScreenHandle screen, UiModelRowsId rows,
                        std::span<const UiRow> items)
{
    Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || !rows.IsValid() || rows.Value > slot->RowLists.size())
        return false;

    Screen::BoundRows& bound = *slot->RowLists[rows.Value - 1];
    if (bound.Items.size() == items.size()
        && std::equal(bound.Items.begin(), bound.Items.end(), items.begin()))
    {
        return false;
    }

    // The document still has elements for every row it last saw, and may read
    // a departing one before the update that removes it.
    std::vector<UiRow> previous = std::exchange(bound.Items, std::vector<UiRow>(items.begin(), items.end()));
    previous.insert(previous.end(), std::make_move_iterator(bound.Departing.begin()),
                    std::make_move_iterator(bound.Departing.end()));
    bound.Departing.assign(std::make_move_iterator(previous.begin() + std::min(items.size(), previous.size())),
                           std::make_move_iterator(previous.end()));
    if (slot->Model != nullptr)
        slot->Model->DirtyVariable(bound.Path);
    return true;
}

std::vector<UiRow> UiRuntime::GetRows(UiScreenHandle screen, UiModelRowsId rows) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || !rows.IsValid() || rows.Value > slot->RowLists.size())
        return {};
    return slot->RowLists[rows.Value - 1]->Items;
}

UiModelRowsId UiRuntime::FindRows(UiScreenHandle screen, std::string_view path) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr)
        return {};
    for (std::size_t i = 0; i < slot->RowLists.size(); ++i)
    {
        if (slot->RowLists[i]->Path == path)
            return UiRowsIdAt(i);
    }
    return {};
}

std::size_t UiRuntime::ArraySize(UiScreenHandle screen, UiModelArrayId array) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || !array.IsValid() || array.Value > slot->Arrays.size())
        return 0;
    return slot->Arrays[array.Value - 1]->Items.size();
}

UiModelArrayId UiRuntime::FindArray(UiScreenHandle screen, std::string_view path) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr)
        return {};
    for (std::size_t i = 0; i < slot->Arrays.size(); ++i)
    {
        if (slot->Arrays[i]->Path == path)
            return UiArrayIdAt(i);
    }
    return {};
}

UiValue UiRuntime::GetValue(UiScreenHandle screen, UiModelPropertyId property) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr || !property.IsValid() || property.Value > slot->Properties.size())
        return {};
    return slot->Properties[property.Value - 1].Value;
}

UiModelPropertyId UiRuntime::FindProperty(UiScreenHandle screen, std::string_view path) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr)
        return {};
    for (std::size_t i = 0; i < slot->Properties.size(); ++i)
    {
        if (slot->Properties[i].Path == path)
            return UiPropertyIdAt(i);
    }
    return {};
}

UiActionId UiRuntime::FindAction(UiScreenHandle screen, std::string_view name) const
{
    const Screen* slot = ResolveScreen(screen);
    if (slot == nullptr)
        return {};
    for (std::size_t i = 0; i < slot->ActionNames.size(); ++i)
    {
        if (slot->ActionNames[i] == name)
            return UiActionIdAt(i);
    }
    return {};
}

std::vector<UiAction> UiRuntime::DrainActions()
{
    return std::exchange(PendingActions, {});
}

std::vector<UiAction> UiRuntime::DrainActions(UiScreenHandle screen)
{
    std::vector<UiAction> taken;
    std::vector<UiAction> left;
    left.reserve(PendingActions.size());
    for (UiAction& action : PendingActions)
    {
        if (action.Screen == screen)
            taken.push_back(std::move(action));
        else
            left.push_back(std::move(action));
    }
    // Order within each screen survives, which is what matters: a begin and the
    // commit that follows it must not arrive the other way round.
    PendingActions = std::move(left);
    return taken;
}
