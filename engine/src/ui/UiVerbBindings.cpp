#include <ui/UiVerbBindings.h>

#include <algorithm>
#include <format>
#include <utility>

namespace
{
    // The conversions a screen may declare, and the whole of them.
    //
    // Presentation values carry what the document engine gave them. Widening an
    // integer to a float is a lossless restatement of the same number; turning
    // an identity into an entity, or a string into a number, is a claim about
    // what the value meant, and nothing here is in a position to make one.
    [[nodiscard]] bool ConversionIsDeclarable(UiValueKind from, AuthoredValueKind to)
    {
        switch (from)
        {
        case UiValueKind::Bool:
            return to == AuthoredValueKind::Bool;
        case UiValueKind::Int:
            return to == AuthoredValueKind::Int || to == AuthoredValueKind::Float;
        case UiValueKind::Float:
            return to == AuthoredValueKind::Float;
        case UiValueKind::String:
            return to == AuthoredValueKind::String || to == AuthoredValueKind::Enum;
        case UiValueKind::Id:
        case UiValueKind::None:
            // An identity is opaque by construction: the document was handed a
            // number it never learns the meaning of, and no mapping here can
            // recover one. A surface that needs to name an entity does it
            // through a host relation, not through the payload.
            return false;
        }
        return false;
    }

    [[nodiscard]] AuthoredValue Convert(const UiValue& value, AuthoredValueKind to)
    {
        switch (to)
        {
        case AuthoredValueKind::Bool:
            return AuthoredValue::Bool(value.AsBool());
        case AuthoredValueKind::Int:
            return AuthoredValue::Int(value.AsInt());
        case AuthoredValueKind::Float:
            return value.Kind() == UiValueKind::Int
                ? AuthoredValue::Float(static_cast<double>(value.AsInt()))
                : AuthoredValue::Float(value.AsFloat());
        case AuthoredValueKind::String:
            return AuthoredValue::String(std::string(value.AsString()));
        case AuthoredValueKind::Enum:
            return AuthoredValue::Enum(std::string(value.AsString()));
        default:
            return {};
        }
    }

    [[nodiscard]] AuthoredValueKind KindOfField(const DataFieldSchema& field)
    {
        // An optional's input is a value for the thing it wraps; a document
        // cannot send "absent", it simply does not raise the action.
        const DataFieldSchema& expected =
            field.Kind == DataFieldKind::Optional && field.Children.size() == 1
            ? field.Children.front()
            : field;

        switch (expected.Kind)
        {
        case DataFieldKind::Bool: return AuthoredValueKind::Bool;
        case DataFieldKind::Int: return AuthoredValueKind::Int;
        case DataFieldKind::Float: return AuthoredValueKind::Float;
        case DataFieldKind::String: return AuthoredValueKind::String;
        case DataFieldKind::Enum: return AuthoredValueKind::Enum;
        case DataFieldKind::Vector: return AuthoredValueKind::Vector;
        case DataFieldKind::Record: return AuthoredValueKind::Record;
        case DataFieldKind::Array: return AuthoredValueKind::Array;
        case DataFieldKind::AssetRef: return AuthoredValueKind::AssetRef;
        case DataFieldKind::DataAssetRef: return AuthoredValueKind::DataAssetRef;
        case DataFieldKind::GameplayTag: return AuthoredValueKind::GameplayTag;
        case DataFieldKind::Entity: return AuthoredValueKind::Entity;
        case DataFieldKind::Optional: break;
        }
        return AuthoredValueKind::None;
    }
}

UiVerbBindings::UiVerbBindings(VerbDispatcher& dispatcher, const VerbBindingSet& bindings)
    : Dispatcher(dispatcher)
    , Bindings(bindings)
{
}

bool UiVerbBindings::Open(UiScreenHandle screen,
                          const UiScreenDesc& desc,
                          std::span<const UiVerbActionMapping> mappings,
                          std::vector<std::string>& errors)
{
    Close();
    if (!screen.IsValid())
    {
        errors.emplace_back("a screen must be open before its actions can be bound");
        return false;
    }

    Desc = desc;
    Mappings.assign(mappings.begin(), mappings.end());
    if (!Compile(errors))
    {
        Desc = {};
        Mappings.clear();
        return false;
    }
    Screen = screen;
    return true;
}

bool UiVerbBindings::Compile(std::vector<std::string>& errors)
{
    std::vector<CompiledAction> compiled;
    compiled.reserve(Mappings.size());
    bool ok = true;

    for (const UiVerbActionMapping& mapping : Mappings)
    {
        // Screen-local ids are positions in the list this opening declared, so
        // they are resolved here rather than remembered from a previous one.
        const auto declared = std::ranges::find(Desc.Actions, mapping.ActionName);
        if (declared == Desc.Actions.end())
        {
            errors.push_back(std::format("this screen declares no action named '{}'",
                                         mapping.ActionName));
            ok = false;
            continue;
        }

        const CompiledVerbBinding* binding = Bindings.Find(mapping.BindingKey);
        if (binding == nullptr)
        {
            errors.push_back(std::format("action '{}' names binding '{}', which did not resolve",
                                         mapping.ActionName, mapping.BindingKey));
            ok = false;
            continue;
        }

        CompiledAction action;
        action.Action =
            UiActionIdAt(static_cast<std::size_t>(declared - Desc.Actions.begin()));
        action.Binding = binding->Key;
        action.InputCount = binding->Inputs.size();

        std::vector<bool> filled(binding->Inputs.size(), false);
        for (const UiVerbArgumentMapping& argument : mapping.Arguments)
        {
            if (argument.InputSlot >= binding->Inputs.size())
            {
                errors.push_back(std::format("action '{}' maps to input slot {}, and binding "
                                             "'{}' declares {}",
                                             mapping.ActionName, argument.InputSlot,
                                             mapping.BindingKey, binding->Inputs.size()));
                ok = false;
                continue;
            }
            const VerbCompiledInput& input = binding->Inputs[argument.InputSlot];
            if (filled[argument.InputSlot])
            {
                errors.push_back(std::format("action '{}' fills input '{}' twice",
                                             mapping.ActionName, input.Name));
                ok = false;
                continue;
            }
            if (!ConversionIsDeclarable(argument.Expected, argument.Produces))
            {
                errors.push_back(std::format("action '{}' declares a conversion this boundary "
                                             "does not perform for input '{}'",
                                             mapping.ActionName, input.Name));
                ok = false;
                continue;
            }
            // Every argument the input feeds has to want what the mapping
            // produces; one value cannot be an integer for one and a string
            // for another.
            bool kindsAgree = !input.Destinations.empty();
            for (const VerbInputDestination& destination : input.Destinations)
                kindsAgree = kindsAgree && argument.Produces == KindOfField(destination.Expected);
            if (!kindsAgree)
            {
                errors.push_back(std::format("action '{}' produces the wrong kind for input '{}'",
                                             mapping.ActionName, input.Name));
                ok = false;
                continue;
            }
            filled[argument.InputSlot] = true;
            action.Arguments.push_back(argument);
        }

        for (std::size_t slot = 0; slot < filled.size(); ++slot)
        {
            if (!filled[slot])
            {
                errors.push_back(std::format("action '{}' leaves binding '{}' input '{}' unfilled",
                                             mapping.ActionName, mapping.BindingKey,
                                             binding->Inputs[slot].Name));
                ok = false;
            }
        }

        for (const CompiledAction& existing : compiled)
        {
            if (existing.Action == action.Action)
            {
                errors.push_back(std::format("action '{}' is mapped twice", mapping.ActionName));
                ok = false;
            }
        }

        compiled.push_back(std::move(action));
    }

    if (!ok)
    {
        Actions.clear();
        CompiledRevision = Bindings.Revision();
        return false;
    }

    Actions = std::move(compiled);
    CompiledRevision = Bindings.Revision();
    return true;
}

void UiVerbBindings::Close()
{
    Screen = {};
    Desc = {};
    Mappings.clear();
    Actions.clear();
    Outcomes.clear();
    Errors.clear();
}

const UiVerbBindings::CompiledAction* UiVerbBindings::Find(UiActionId action) const
{
    for (const CompiledAction& compiled : Actions)
    {
        if (compiled.Action == action)
            return &compiled;
    }
    return nullptr;
}

void UiVerbBindings::Dispatch(std::span<const UiAction> actions)
{
    Outcomes.clear();
    if (!IsOpen())
        return;

    // The set moved under an open screen: compiled again against what it holds
    // now. A mapping that no longer compiles leaves every action refusing,
    // with the reasons in LastErrors, rather than acting on stale slots.
    if (CompiledRevision != Bindings.Revision())
    {
        Errors.clear();
        (void)Compile(Errors);
    }

    for (const UiAction& action : actions)
    {
        if (action.Screen != Screen)
            continue;
        const CompiledAction* compiled = Find(action.Id);
        if (compiled == nullptr)
            continue;

        const CompiledVerbBinding* binding = Bindings.Find(compiled->Binding);
        if (binding == nullptr)
        {
            Outcomes.push_back(Outcome{ .Action = action.Id,
                                        .Status = VerbAdmission::UnresolvedBinding,
                                        .Id = {} });
            continue;
        }

        Inputs.assign(compiled->InputCount, AuthoredValue{});
        bool payloadIsRight = true;
        for (const UiVerbArgumentMapping& argument : compiled->Arguments)
        {
            if (argument.ArgumentIndex >= action.Arguments.size()
                || action.Arguments[argument.ArgumentIndex].Kind() != argument.Expected)
            {
                // The document sent something other than what the mapping
                // declared. Refused whole rather than partly filled: half an
                // argument list is a different request.
                payloadIsRight = false;
                break;
            }
            Inputs[argument.InputSlot] =
                Convert(action.Arguments[argument.ArgumentIndex], argument.Produces);
        }

        if (!payloadIsRight)
        {
            Outcomes.push_back(Outcome{ .Action = action.Id,
                                        .Status = VerbAdmission::InvalidArguments,
                                        .Id = {} });
            continue;
        }

        const VerbInvocationResult result =
            Dispatcher.Invoke(*binding, Inputs, VerbInvocationSource{ .Parent = {}, .Producer = {}, .Instigator = Instigator, .Tick = 0 });
        Outcomes.push_back(
            Outcome{ .Action = action.Id, .Status = result.Status, .Id = result.Id });
    }
}
