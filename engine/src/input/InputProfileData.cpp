#include <input/InputProfileData.h>

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/json/JsonValue.h>

#include <format>
#include <memory>
#include <unordered_set>
#include <utility>

namespace
{
DataFieldSchema StringField(std::string key, std::string display, std::string summary,
                            bool required = true)
{
    DataFieldSchema field;
    field.Key = std::move(key);
    field.DisplayName = std::move(display);
    field.Summary = std::move(summary);
    field.Kind = DataFieldKind::String;
    field.Required = required;
    return field;
}

DataFieldSchema FloatField(std::string key, std::string display, std::string summary,
                           double minimum, double maximum)
{
    DataFieldSchema field;
    field.Key = std::move(key);
    field.DisplayName = std::move(display);
    field.Summary = std::move(summary);
    field.Kind = DataFieldKind::Float;
    field.Numeric.Minimum = minimum;
    field.Numeric.Maximum = maximum;
    field.Required = false;
    return field;
}

DataFieldSchema BoolField(std::string key, std::string display, std::string summary)
{
    DataFieldSchema field;
    field.Key = std::move(key);
    field.DisplayName = std::move(display);
    field.Summary = std::move(summary);
    field.Kind = DataFieldKind::Bool;
    field.Required = false;
    return field;
}

DataSchema MakeActionSetSchema()
{
    DataFieldSchema type;
    type.Key = "type";
    type.DisplayName = "Value";
    type.Summary = "What shape this action carries.";
    type.Kind = DataFieldKind::Enum;
    type.Default = std::string("digital");
    type.Required = false;
    type.EnumChoices = {
        { "digital", "Button", "On or off." },
        { "axis1", "Axis", "One signed scalar." },
        { "axis2", "Plane", "Two signed scalars, such as movement or look." },
    };

    DataFieldSchema scope;
    scope.Key = "scope";
    scope.DisplayName = "Scope";
    scope.Summary = "Whether the simulation consumes this action or only the presentation layer does.";
    scope.Kind = DataFieldKind::Enum;
    scope.Default = std::string("simulation");
    scope.Required = false;
    scope.EnumChoices = {
        { "simulation", "Simulation", "Drives the simulation, and may travel in a player command." },
        { "presentation", "Presentation", "Local to this client: menus, debug toggles." },
    };

    DataFieldSchema fire;
    fire.Key = "fire";
    fire.DisplayName = "Fires";
    fire.Summary = "When a button action counts as fired. Gameplay that reacts to one moment reads this; gameplay built out of several phases reads the raw edges instead.";
    fire.Kind = DataFieldKind::Enum;
    fire.Default = std::string("pressed");
    fire.Required = false;
    fire.EnumChoices = {
        { "pressed", "On press", "Once, the moment the control goes down." },
        { "released", "On release", "Once, the moment the control comes up." },
        { "held", "While held", "Every pass the control is down, which repeats." },
    };

    DataFieldSchema action;
    action.Key = "action";
    action.DisplayName = "Action";
    action.Kind = DataFieldKind::Record;
    action.Children = {
        StringField("name", "Name", "Identity referenced by profiles and by gameplay code."),
        std::move(type),
        std::move(scope),
        std::move(fire),
    };

    DataFieldSchema actions;
    actions.Key = "actions";
    actions.DisplayName = "Actions";
    actions.Summary = "Every action this game understands. Bindings and gameplay both refer to these by name.";
    actions.Kind = DataFieldKind::Array;
    actions.Editor.Widget = "cards";
    actions.Editor.TitleKey = "name";
    actions.Children = { std::move(action) };

    DataSchema schema;
    schema.TypeName = std::string(kInputActionSetTypeName);
    schema.DisplayName = "Input actions";
    schema.Description = "The action vocabulary a game's input profiles bind controls to.";
    schema.Root.Kind = DataFieldKind::Record;
    schema.Root.Children = { std::move(actions) };
    return schema;
}

DataSchema MakeProfileSchema()
{
    DataFieldSchema composite;
    composite.Key = "composite";
    composite.DisplayName = "Composite";
    composite.Summary = "Build the value from several buttons instead of one control.";
    composite.Kind = DataFieldKind::Enum;
    composite.Required = false;
    composite.EnumChoices = {
        { "axis", "Axis pair", "Two buttons drive one signed scalar." },
        { "cardinal", "Four directions", "Four buttons drive a plane, as WASD does." },
    };

    DataFieldSchema binding;
    binding.Key = "binding";
    binding.DisplayName = "Binding";
    binding.Summary = "One way to produce an action: a single control, or several buttons combined.";
    binding.Kind = DataFieldKind::Record;
    binding.Editor.Widget = "compact";
    binding.Children = {
        StringField("action", "Action", "The action this control drives, by the name the action set declares."),
        StringField("control", "Control", "The one control that drives this action. Leave empty when several buttons combine into it instead.", false),
        std::move(composite),
        StringField("negative", "Negative", "The button that drives this axis negative.", false),
        StringField("positive", "Positive", "The button that drives this axis positive.", false),
        StringField("left", "Left", "The button that steers left.", false),
        StringField("right", "Right", "The button that steers right.", false),
        StringField("down", "Down", "The button that steers back or down.", false),
        StringField("up", "Up", "The button that steers forward or up.", false),
        FloatField("scale", "Scale", "Multiplier applied to the resolved value.", -1000.0, 1000.0),
        FloatField("dead_zone", "Dead zone", "Magnitude below which the control reads as rest.", 0.0, 0.99),
        FloatField("threshold", "Threshold", "How far a trigger must be pulled before it counts as pressed.", 0.01, 1.0),
        BoolField("invert_x", "Invert X", "Flip the sign of the X result."),
        BoolField("invert_y", "Invert Y", "Flip the sign of the Y result."),
        BoolField("normalize", "Clamp to unit", "Keep a cardinal composite inside the unit circle so diagonals are not faster."),
    };

    DataFieldSchema bindings;
    bindings.Key = "bindings";
    bindings.DisplayName = "Bindings";
    bindings.Kind = DataFieldKind::Array;
    bindings.Editor.Widget = "cards";
    bindings.Editor.TitleKey = "action";
    bindings.Children = { std::move(binding) };

    DataFieldSchema priority;
    priority.Key = "priority";
    priority.DisplayName = "Priority";
    priority.Summary = "Higher priority claims a shared control first. Unique within a profile.";
    priority.Kind = DataFieldKind::Int;
    priority.Required = true;

    DataFieldSchema context;
    context.Key = "context";
    context.DisplayName = "Context";
    context.Summary = "A group of bindings that can be switched on and off while the game runs.";
    context.Kind = DataFieldKind::Record;
    context.Children = {
        StringField("name", "Name", "Identity used to activate and deactivate this context at runtime."),
        std::move(priority),
        std::move(bindings),
    };

    DataFieldSchema contexts;
    contexts.Key = "contexts";
    contexts.DisplayName = "Contexts";
    contexts.Summary = "Binding groups that switch on and off independently. Where two active groups bind the same control, the higher priority takes it and the lower one stops seeing it.";
    contexts.Kind = DataFieldKind::Array;
    contexts.Editor.Widget = "cards";
    contexts.Editor.TitleKey = "name";
    contexts.Children = { std::move(context) };

    DataFieldSchema actionSet;
    actionSet.Key = "actions";
    actionSet.DisplayName = "Action set";
    actionSet.Summary = "The input.actions asset whose actions these bindings name.";
    actionSet.Kind = DataFieldKind::DataAssetRef;
    actionSet.Reference.AssetTypeFilter = AssetType::Data;
    actionSet.Reference.DataSubtype = std::string(kInputActionSetTypeName);
    actionSet.Required = true;

    DataSchema schema;
    schema.TypeName = std::string(kInputProfileTypeName);
    schema.DisplayName = "Input profile";
    schema.Description = "Controls bound to actions, grouped into activatable contexts.";
    schema.Root.Kind = DataFieldKind::Record;
    schema.Root.Children = { std::move(actionSet), std::move(contexts) };
    return schema;
}

bool ParseActionType(std::string_view text, InputActionType& out)
{
    if (text == "digital") { out = InputActionType::Digital; return true; }
    if (text == "axis1")   { out = InputActionType::Axis1D;  return true; }
    if (text == "axis2")   { out = InputActionType::Axis2D;  return true; }
    return false;
}

bool ParseActionScope(std::string_view text, InputActionScope& out)
{
    if (text == "simulation")   { out = InputActionScope::Simulation;   return true; }
    if (text == "presentation") { out = InputActionScope::Presentation; return true; }
    return false;
}

bool ParseFireMode(std::string_view text, InputActionFireMode& out)
{
    if (text == "pressed")  { out = InputActionFireMode::Pressed;  return true; }
    if (text == "released") { out = InputActionFireMode::Released; return true; }
    if (text == "held")     { out = InputActionFireMode::Held;     return true; }
    return false;
}

// Reads a control name from a member, reporting the exact path on failure so a
// typo names itself instead of producing a control that never fires.
bool ReadControl(const JsonValue& owner,
                 std::string_view key,
                 std::string_view path,
                 bool required,
                 InputControl& out,
                 std::string& error)
{
    const JsonValue* value = owner.Find(key);
    if (value == nullptr || value->IsNull())
    {
        if (!required)
            return true;
        error = std::format("{}.{} is required", path, key);
        return false;
    }
    if (!value->IsString())
    {
        error = std::format("{}.{} must be a control name", path, key);
        return false;
    }

    const std::optional<InputControl> control = ParseInputControl(value->AsString());
    if (!control.has_value())
    {
        error = std::format("{}.{} names no control: '{}'", path, key, value->AsString());
        return false;
    }
    out = *control;
    return true;
}

void ReadFloat(const JsonValue& owner, std::string_view key, float& out)
{
    const JsonValue* value = owner.Find(key);
    if (value != nullptr && value->IsNumber())
        out = static_cast<float>(value->AsNumber());
}

void ReadBool(const JsonValue& owner, std::string_view key, bool& out)
{
    const JsonValue* value = owner.Find(key);
    if (value != nullptr && value->IsBool())
        out = value->AsBool();
}

bool CompileBinding(const JsonValue& item,
                    const std::string& path,
                    AuthoredInputBinding& out,
                    std::string& error)
{
    if (!item.IsObject())
    {
        error = std::format("{} must be an object", path);
        return false;
    }

    const JsonValue* action = item.Find("action");
    if (action == nullptr || !action->IsString() || action->AsString().empty())
    {
        error = std::format("{}.action is required", path);
        return false;
    }
    out.Action = action->AsString();

    const JsonValue* composite = item.Find("composite");
    const JsonValue* control = item.Find("control");
    const bool hasComposite = composite != nullptr && composite->IsString();
    const bool hasControl = control != nullptr && !control->IsNull();

    if (hasComposite == hasControl)
    {
        error = std::format("{} needs exactly one of control or composite", path);
        return false;
    }

    if (!hasComposite)
    {
        out.Binding.Kind = InputBindingKind::Direct;
        if (!ReadControl(item, "control", path, true, out.Binding.Controls[kBindingNegativeX], error))
            return false;
    }
    else if (composite->AsString() == "axis")
    {
        out.Binding.Kind = InputBindingKind::AxisPair;
        if (!ReadControl(item, "negative", path, true, out.Binding.Controls[kBindingNegativeX], error)
            || !ReadControl(item, "positive", path, true, out.Binding.Controls[kBindingPositiveX], error))
        {
            return false;
        }
    }
    else if (composite->AsString() == "cardinal")
    {
        out.Binding.Kind = InputBindingKind::Cardinal;
        if (!ReadControl(item, "left", path, true, out.Binding.Controls[kBindingNegativeX], error)
            || !ReadControl(item, "right", path, true, out.Binding.Controls[kBindingPositiveX], error)
            || !ReadControl(item, "down", path, true, out.Binding.Controls[kBindingNegativeY], error)
            || !ReadControl(item, "up", path, true, out.Binding.Controls[kBindingPositiveY], error))
        {
            return false;
        }
    }
    else
    {
        error = std::format("{}.composite is not a composite kind: '{}'", path, composite->AsString());
        return false;
    }

    for (const InputControl& bound : out.Binding.Controls)
    {
        if (bound.IsValid() && !BindingKindAcceptsControl(out.Binding.Kind, bound.Source))
        {
            error = std::format("{} uses '{}' in a composite, which is not a button",
                                path, FormatInputControl(bound));
            return false;
        }
    }

    ReadFloat(item, "scale", out.Binding.Scale);
    ReadFloat(item, "dead_zone", out.Binding.DeadZone);
    ReadFloat(item, "threshold", out.Binding.Threshold);
    ReadBool(item, "invert_x", out.Binding.InvertX);
    ReadBool(item, "invert_y", out.Binding.InvertY);
    ReadBool(item, "normalize", out.Binding.Normalize);
    return true;
}

DataAssetCompileResult CompileInputActionSet(const JsonValue& data)
{
    DataAssetCompileResult result;
    auto actionSet = std::make_shared<CompiledInputActionSet>();

    const JsonValue* actions = data.Find("actions");
    if (actions == nullptr || !actions->IsArray())
    {
        result.Error = "$.data.actions must be an array";
        return result;
    }

    std::unordered_set<std::string> seen;
    for (std::size_t index = 0; index < actions->AsArray().size(); ++index)
    {
        const JsonValue& item = actions->AsArray()[index];
        const std::string path = std::format("$.data.actions[{}]", index);
        if (!item.IsObject())
        {
            result.Error = std::format("{} must be an object", path);
            return result;
        }

        const JsonValue* name = item.Find("name");
        if (name == nullptr || !name->IsString() || name->AsString().empty())
        {
            result.Error = std::format("{}.name is required", path);
            return result;
        }

        // Two definitions of one action mean two sets of bindings silently
        // claiming the same intent.
        if (!seen.insert(name->AsString()).second)
        {
            result.Error = std::format("{}.name duplicates action '{}'", path, name->AsString());
            return result;
        }

        InputActionDefinition definition;
        definition.Name = name->AsString();

        if (const JsonValue* type = item.Find("type"); type != nullptr && type->IsString())
        {
            if (!ParseActionType(type->AsString(), definition.Type))
            {
                result.Error = std::format("{}.type is not an action type: '{}'", path, type->AsString());
                return result;
            }
        }
        if (const JsonValue* scope = item.Find("scope"); scope != nullptr && scope->IsString())
        {
            if (!ParseActionScope(scope->AsString(), definition.Scope))
            {
                result.Error = std::format("{}.scope is not a scope: '{}'", path, scope->AsString());
                return result;
            }
        }
        if (const JsonValue* fire = item.Find("fire"); fire != nullptr && !fire->IsNull())
        {
            // Authored on an axis action the knob would silently never apply,
            // which reads as a mapping that does not work. Naming the path is
            // how the author finds out it belongs on a button.
            if (definition.Type != InputActionType::Digital)
            {
                result.Error = std::format("{}.fire applies only to a digital action", path);
                return result;
            }
            if (!fire->IsString() || !ParseFireMode(fire->AsString(), definition.Fire))
            {
                result.Error = std::format("{}.fire is not a fire mode: '{}'", path,
                                           fire->IsString() ? fire->AsString() : std::string("<not a string>"));
                return result;
            }
        }

        actionSet->Actions.push_back(std::move(definition));
    }

    result.Value = std::move(actionSet);
    return result;
}

DataAssetCompileResult CompileInputProfile(const JsonValue& data)
{
    DataAssetCompileResult result;
    auto profile = std::make_shared<CompiledInputProfile>();

    const JsonValue* actionSet = data.Find("actions");
    if (actionSet == nullptr || !actionSet->IsString() || actionSet->AsString().empty())
    {
        result.Error = "$.data.actions must name an input.actions asset";
        return result;
    }
    profile->ActionSetPath = actionSet->AsString();

    const JsonValue* contexts = data.Find("contexts");
    if (contexts == nullptr || !contexts->IsArray())
    {
        result.Error = "$.data.contexts must be an array";
        return result;
    }

    std::unordered_set<std::string> seenNames;
    std::unordered_set<std::int32_t> seenPriorities;
    for (std::size_t index = 0; index < contexts->AsArray().size(); ++index)
    {
        const JsonValue& item = contexts->AsArray()[index];
        const std::string path = std::format("$.data.contexts[{}]", index);
        if (!item.IsObject())
        {
            result.Error = std::format("{} must be an object", path);
            return result;
        }

        const JsonValue* name = item.Find("name");
        if (name == nullptr || !name->IsString() || name->AsString().empty())
        {
            result.Error = std::format("{}.name is required", path);
            return result;
        }
        if (!seenNames.insert(name->AsString()).second)
        {
            result.Error = std::format("{}.name duplicates context '{}'", path, name->AsString());
            return result;
        }

        const JsonValue* priority = item.Find("priority");
        if (priority == nullptr || !priority->IsNumber())
        {
            result.Error = std::format("{}.priority is required", path);
            return result;
        }

        AuthoredInputContext context;
        context.Name = name->AsString();
        context.Priority = static_cast<std::int32_t>(priority->AsNumber());

        // Equal priorities would make claim order depend on authoring order,
        // which is exactly the hidden rule contexts are meant to avoid.
        if (!seenPriorities.insert(context.Priority).second)
        {
            result.Error = std::format("{}.priority {} is already used by another context",
                                       path, context.Priority);
            return result;
        }

        const JsonValue* bindings = item.Find("bindings");
        if (bindings == nullptr || !bindings->IsArray())
        {
            result.Error = std::format("{}.bindings must be an array", path);
            return result;
        }

        for (std::size_t bindingIndex = 0; bindingIndex < bindings->AsArray().size(); ++bindingIndex)
        {
            AuthoredInputBinding binding;
            if (!CompileBinding(bindings->AsArray()[bindingIndex],
                                std::format("{}.bindings[{}]", path, bindingIndex),
                                binding,
                                result.Error))
            {
                return result;
            }
            context.Bindings.push_back(std::move(binding));
        }

        profile->Contexts.push_back(std::move(context));
    }

    // The action set has to be resident before these bindings can resolve, so
    // the loader stages it with the profile rather than after it.
    result.Dependencies.push_back(AssetRef{ AssetType::Data, profile->ActionSetPath });
    result.Value = std::move(profile);
    return result;
}
}

void RegisterInputProfileData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    DataAssetTypeRegistration actionSet;
    actionSet.Name = std::string(kInputActionSetTypeName);
    actionSet.CurrentVersion = 1;
    actionSet.Compile = CompileInputActionSet;
    if (!types.Register(std::move(actionSet)))
        return;

    if (!schemas.Register(MakeActionSetSchema()))
    {
        (void)types.Unregister(kInputActionSetTypeName);
        return;
    }

    DataAssetTypeRegistration profile;
    profile.Name = std::string(kInputProfileTypeName);
    profile.CurrentVersion = 1;
    profile.Compile = CompileInputProfile;
    if (!types.Register(std::move(profile)))
    {
        (void)types.Unregister(kInputActionSetTypeName);
        (void)schemas.Unregister(kInputActionSetTypeName);
        return;
    }

    if (!schemas.Register(MakeProfileSchema()))
    {
        (void)types.Unregister(kInputProfileTypeName);
        (void)types.Unregister(kInputActionSetTypeName);
        (void)schemas.Unregister(kInputActionSetTypeName);
    }
}

void UnregisterInputProfileData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (types.Unregister(kInputProfileTypeName))
        (void)schemas.Unregister(kInputProfileTypeName);
    if (types.Unregister(kInputActionSetTypeName))
        (void)schemas.Unregister(kInputActionSetTypeName);
}
