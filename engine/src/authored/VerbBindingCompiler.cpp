#include <authored/VerbBindingCompiler.h>

#include <core/identity/Id.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/identity/PersistentEntityIndex.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

namespace
{
    void Fail(std::vector<std::string>& errors,
              std::string_view bindingKey,
              std::string_view argumentPath,
              std::string message)
    {
        if (argumentPath.empty())
            errors.push_back(std::format("binding '{}': {}", bindingKey, message));
        else
            errors.push_back(
                std::format("binding '{}' argument '{}': {}", bindingKey, argumentPath, message));
    }

    // JSON numbers are doubles. An identity, a count, or a frame budget that
    // arrived as 2^53 + 1 would silently become something else, so an integer
    // that cannot be represented exactly is refused rather than rounded.
    [[nodiscard]] bool ExactInteger(double value, std::int64_t& out)
    {
        if (!std::isfinite(value) || std::floor(value) != value)
            return false;
        constexpr double kMaxExact = 9007199254740992.0; // 2^53
        if (value > kMaxExact || value < -kMaxExact)
            return false;
        out = static_cast<std::int64_t>(value);
        return true;
    }

    [[nodiscard]] bool WithinRange(double value, const DataFieldSchema& field)
    {
        if (field.Numeric.Minimum && value < *field.Numeric.Minimum)
            return false;
        if (field.Numeric.Maximum && value > *field.Numeric.Maximum)
            return false;
        return true;
    }

    [[nodiscard]] std::string ElementPath(std::string_view parent, std::size_t index)
    {
        return std::format("{}[{}]", parent, index);
    }

    [[nodiscard]] std::string MemberPath(std::string_view parent, std::string_view key)
    {
        return parent.empty() ? std::string(key) : std::format("{}.{}", parent, key);
    }

    // A literal in the authored file, against the field the verb declares.
    // Typed, never opportunistic: "3" stays a string and fails an Int field
    // rather than becoming three.
    bool CompileLiteral(const JsonValue& value,
                        const DataFieldSchema& field,
                        std::string_view bindingKey,
                        const std::string& path,
                        VerbValue& out,
                        std::vector<std::string>& errors);

    bool CompileDefault(const DataFieldSchema& field,
                        std::string_view bindingKey,
                        const std::string& path,
                        VerbValue& out,
                        std::vector<std::string>& errors)
    {
        // One definition of what an unsupplied argument means, here rather than
        // in each consumer: an explicit default if the field has one, absent if
        // the field tolerates absence, and a diagnostic otherwise.
        if (std::holds_alternative<bool>(field.Default))
            return CompileLiteral(JsonValue(std::get<bool>(field.Default)), field, bindingKey,
                                  path, out, errors);
        if (std::holds_alternative<std::int64_t>(field.Default))
            return CompileLiteral(
                JsonValue(static_cast<double>(std::get<std::int64_t>(field.Default))), field,
                bindingKey, path, out, errors);
        if (std::holds_alternative<double>(field.Default))
            return CompileLiteral(JsonValue(std::get<double>(field.Default)), field, bindingKey,
                                  path, out, errors);
        if (std::holds_alternative<std::string>(field.Default))
            return CompileLiteral(JsonValue(std::get<std::string>(field.Default)), field,
                                  bindingKey, path, out, errors);

        if (field.Required)
        {
            Fail(errors, bindingKey, path, "the verb requires this argument and the binding "
                                           "supplies neither a value nor an input");
            return false;
        }
        out = VerbValue{};
        return true;
    }

    bool CompileLiteral(const JsonValue& value,
                        const DataFieldSchema& field,
                        std::string_view bindingKey,
                        const std::string& path,
                        VerbValue& out,
                        std::vector<std::string>& errors)
    {
        switch (field.Kind)
        {
        case DataFieldKind::Bool:
            if (!value.IsBool())
            {
                Fail(errors, bindingKey, path, "expected a boolean");
                return false;
            }
            out = VerbValue::Bool(value.AsBool());
            return true;

        case DataFieldKind::Int:
        {
            std::int64_t whole = 0;
            if (!value.IsNumber() || !ExactInteger(value.AsNumber(), whole))
            {
                Fail(errors, bindingKey, path,
                     "expected an integer a 64-bit value can hold exactly");
                return false;
            }
            if (!WithinRange(value.AsNumber(), field))
            {
                Fail(errors, bindingKey, path, "value is outside the declared range");
                return false;
            }
            out = VerbValue::Int(whole);
            return true;
        }

        case DataFieldKind::Float:
            if (!value.IsNumber() || !std::isfinite(value.AsNumber()))
            {
                Fail(errors, bindingKey, path, "expected a finite number");
                return false;
            }
            if (!WithinRange(value.AsNumber(), field))
            {
                Fail(errors, bindingKey, path, "value is outside the declared range");
                return false;
            }
            out = VerbValue::Float(value.AsNumber());
            return true;

        case DataFieldKind::String:
            if (!value.IsString())
            {
                Fail(errors, bindingKey, path, "expected a string");
                return false;
            }
            out = VerbValue::String(value.AsString());
            return true;

        case DataFieldKind::Enum:
        {
            if (!value.IsString())
            {
                Fail(errors, bindingKey, path, "expected one of the declared choices");
                return false;
            }
            const bool known = std::ranges::any_of(
                field.EnumChoices,
                [&value](const DataEnumChoice& choice) { return choice.Value == value.AsString(); });
            if (!known)
            {
                Fail(errors, bindingKey, path,
                     std::format("'{}' is not one of the declared choices", value.AsString()));
                return false;
            }
            out = VerbValue::Enum(value.AsString());
            return true;
        }

        case DataFieldKind::Vector:
        {
            if (!value.IsArray() || value.AsArray().size() != field.VectorLength)
            {
                Fail(errors, bindingKey, path,
                     std::format("expected {} numbers", field.VectorLength));
                return false;
            }
            VerbVectorValue vector;
            vector.Length = static_cast<std::uint8_t>(field.VectorLength);
            for (std::size_t index = 0; index < field.VectorLength; ++index)
            {
                const JsonValue& element = value.AsArray()[index];
                if (!element.IsNumber() || !std::isfinite(element.AsNumber())
                    || !WithinRange(element.AsNumber(), field))
                {
                    Fail(errors, bindingKey, ElementPath(path, index),
                         "expected a finite number within the declared range");
                    return false;
                }
                vector.Components[index] = element.AsNumber();
            }
            out = VerbValue::Vector(vector);
            return true;
        }

        case DataFieldKind::Record:
        {
            if (!value.IsObject())
            {
                Fail(errors, bindingKey, path, "expected an object");
                return false;
            }
            std::vector<VerbValue> members;
            members.reserve(field.Children.size());
            bool ok = true;
            for (const DataFieldSchema& child : field.Children)
            {
                const std::string childPath = MemberPath(path, child.Key);
                const JsonValue* member = value.Find(child.Key);
                VerbValue compiled;
                if (member == nullptr)
                    ok = CompileDefault(child, bindingKey, childPath, compiled, errors) && ok;
                else
                    ok = CompileLiteral(*member, child, bindingKey, childPath, compiled, errors)
                        && ok;
                members.push_back(std::move(compiled));
            }
            for (const auto& [key, unused] : value.AsObject())
            {
                (void)unused;
                if (FindChild(field, key) == nullptr)
                {
                    Fail(errors, bindingKey, MemberPath(path, key),
                         "the verb's contract does not accept this member");
                    ok = false;
                }
            }
            if (!ok)
                return false;
            out = VerbValue::Record(std::move(members));
            return true;
        }

        case DataFieldKind::Array:
        {
            if (!value.IsArray())
            {
                Fail(errors, bindingKey, path, "expected an array");
                return false;
            }
            if (field.Children.size() != 1)
            {
                Fail(errors, bindingKey, path, "the verb's array argument names no element shape");
                return false;
            }
            std::vector<VerbValue> elements;
            elements.reserve(value.AsArray().size());
            bool ok = true;
            for (std::size_t index = 0; index < value.AsArray().size(); ++index)
            {
                VerbValue element;
                ok = CompileLiteral(value.AsArray()[index], field.Children.front(), bindingKey,
                                    ElementPath(path, index), element, errors)
                    && ok;
                elements.push_back(std::move(element));
            }
            if (!ok)
                return false;
            out = VerbValue::Array(std::move(elements));
            return true;
        }

        case DataFieldKind::Optional:
            // An explicit null is a value: the author said "nothing here". It is
            // not the same as leaving the argument out, which takes the default.
            if (value.IsNull())
            {
                out = VerbValue{};
                return true;
            }
            if (field.Children.size() != 1)
            {
                Fail(errors, bindingKey, path, "the verb's optional argument names no value shape");
                return false;
            }
            return CompileLiteral(value, field.Children.front(), bindingKey, path, out, errors);

        case DataFieldKind::AssetRef:
        case DataFieldKind::DataAssetRef:
            Fail(errors, bindingKey, path,
                 "an asset argument is written as {\"asset\": ...} or {\"data\": ...} so the "
                 "dependency can be named before a World exists");
            return false;

        case DataFieldKind::GameplayTag:
            Fail(errors, bindingKey, path,
                 "a gameplay tag argument is written as {\"tag\": ...}");
            return false;

        case DataFieldKind::Entity:
            Fail(errors, bindingKey, path,
                 "an entity argument is written as {\"entity\": ...}");
            return false;
        }

        Fail(errors, bindingKey, path, "the verb declares a shape this binding cannot compile");
        return false;
    }

    // The field an argument source actually has to satisfy. An optional wraps
    // its element, and a reference source names the element rather than the
    // wrapper, so "Optional<AssetRef>" accepts {"asset": ...} without the author
    // spelling the optionality.
    [[nodiscard]] const DataFieldSchema& Unwrap(const DataFieldSchema& field)
    {
        if (field.Kind == DataFieldKind::Optional && field.Children.size() == 1)
            return field.Children.front();
        return field;
    }

    bool CompileReference(const VerbBindingArgument& argument,
                          const DataFieldSchema& field,
                          const VerbBindingEnvironment& environment,
                          std::string_view bindingKey,
                          VerbValue& out,
                          std::vector<std::string>& errors)
    {
        const DataFieldSchema& expected = Unwrap(field);
        switch (argument.Source)
        {
        case VerbArgumentSource::Asset:
        case VerbArgumentSource::DataAsset:
        {
            const bool wantsData = argument.Source == VerbArgumentSource::DataAsset;
            const DataFieldKind required =
                wantsData ? DataFieldKind::DataAssetRef : DataFieldKind::AssetRef;
            if (expected.Kind != required)
            {
                Fail(errors, bindingKey, argument.Key,
                     wantsData ? "the verb does not declare this argument as a data asset"
                               : "the verb does not declare this argument as an asset");
                return false;
            }
            if (!argument.Text.starts_with("asset://"))
            {
                Fail(errors, bindingKey, argument.Key, "an asset path starts with 'asset://'");
                return false;
            }
            AssetRef reference;
            // The kind comes from the contract, not from the path: an authored
            // string cannot claim to be a texture where the verb wants a mesh.
            reference.Type = wantsData ? AssetType::Data : expected.Reference.AssetTypeFilter;
            reference.Path = argument.Text;
            out = wantsData ? VerbValue::DataAsset(std::move(reference))
                            : VerbValue::Asset(std::move(reference));
            return true;
        }

        case VerbArgumentSource::Tag:
        {
            if (expected.Kind != DataFieldKind::GameplayTag)
            {
                Fail(errors, bindingKey, argument.Key,
                     "the verb does not declare this argument as a gameplay tag");
                return false;
            }
            if (environment.Tags == nullptr)
            {
                Fail(errors, bindingKey, argument.Key,
                     "this World has no gameplay tag vocabulary to resolve the tag against");
                return false;
            }
            const GameplayTagId tag = environment.Tags->FindTag(argument.Text);
            if (!tag.IsValid())
            {
                Fail(errors, bindingKey, argument.Key,
                     std::format("'{}' is not a tag this World declares", argument.Text));
                return false;
            }
            out = VerbValue::Tag(tag);
            return true;
        }

        case VerbArgumentSource::Entity:
        {
            if (expected.Kind != DataFieldKind::Entity)
            {
                Fail(errors, bindingKey, argument.Key,
                     "the verb does not declare this argument as an entity");
                return false;
            }
            const std::optional<PersistentEntityId> identity =
                PersistentEntityIdFromString(argument.Text);
            if (!identity.has_value())
            {
                Fail(errors, bindingKey, argument.Key,
                     "expected 16 lowercase hex digits naming a persistent entity");
                return false;
            }
            if (environment.Entities == nullptr)
            {
                Fail(errors, bindingKey, argument.Key,
                     "this World keeps no persistent entity index to resolve the reference "
                     "against");
                return false;
            }
            const EntityId entity = environment.Entities->TryResolve(*identity);
            if (!entity.IsValid())
            {
                Fail(errors, bindingKey, argument.Key,
                     std::format("no entity in this World carries the identity '{}'",
                                 argument.Text));
                return false;
            }
            out = VerbValue::Entity(entity);
            return true;
        }

        case VerbArgumentSource::Literal:
        case VerbArgumentSource::Input:
            break;
        }
        Fail(errors, bindingKey, argument.Key, "unsupported argument source");
        return false;
    }
}

bool CompileVerbBinding(const VerbBindingDesc& desc,
                        const VerbBindingEnvironment& environment,
                        CompiledVerbBinding& out,
                        std::vector<std::string>& errors)
{
    if (environment.Verbs == nullptr)
    {
        Fail(errors, desc.Key, {}, "no catalog to resolve the verb name against");
        return false;
    }

    for (std::size_t index = 0; index + 1 < desc.Inputs.size(); ++index)
    {
        // Positional supply means a repeated name has no answer to "which slot
        // did the producer mean".
        const auto duplicate = std::ranges::find(desc.Inputs.begin() + index + 1,
                                                 desc.Inputs.end(), desc.Inputs[index]);
        if (duplicate != desc.Inputs.end())
        {
            Fail(errors, desc.Key, {},
                 std::format("input '{}' is declared twice", desc.Inputs[index]));
            return false;
        }
    }

    const VerbRegistry& registry = *environment.Verbs;
    const VerbId verb = registry.Find(desc.VerbName);
    const VerbDefinition* definition = registry.Get(verb);
    if (definition == nullptr)
    {
        // The record survives: an unresolved binding is inspectable content, not
        // a reason to lose what the author wrote.
        Fail(errors, desc.Key, {},
             std::format("'{}' is not a verb this World declares", desc.VerbName));
        return false;
    }

    const DataFieldSchema& root = definition->Arguments;
    CompiledVerbBinding compiled;
    compiled.Catalog = registry.Catalog();
    compiled.Verb = verb;
    compiled.Revision = registry.Revision(verb);
    compiled.Key = desc.KeyId.IsValid() ? desc.KeyId : MakeVerbBindingKey(desc.Key);
    compiled.KeyText = desc.Key;
    compiled.Constants = VerbArguments(root.Children.size());

    bool ok = true;

    // Which argument each declared input fills, and which argument a source
    // already claimed. Two channels writing one argument is exactly the
    // ambiguity that lets a caller smuggle a second target past validation.
    std::vector<bool> filled(root.Children.size(), false);
    std::vector<VerbCompiledInput> inputs;
    inputs.reserve(desc.Inputs.size());

    for (const VerbBindingArgument& argument : desc.Arguments)
    {
        const DataFieldSchema* field = FindChild(root, argument.Key);
        if (field == nullptr)
        {
            Fail(errors, desc.Key, argument.Key,
                 std::format("'{}' declares no argument by this name", desc.VerbName));
            ok = false;
            continue;
        }
        // FindChild returns a pointer into root.Children, so its position is the
        // argument's slot. The order is the schema's and is decided once here.
        const std::size_t slot = static_cast<std::size_t>(field - root.Children.data());
        if (filled[slot])
        {
            Fail(errors, desc.Key, argument.Key, "the binding fills this argument twice");
            ok = false;
            continue;
        }
        filled[slot] = true;

        if (argument.Source == VerbArgumentSource::Input)
        {
            const auto declared = std::ranges::find(desc.Inputs, argument.Text);
            if (declared == desc.Inputs.end())
            {
                Fail(errors, desc.Key, argument.Key,
                     std::format("'{}' is not one of this binding's declared inputs",
                                 argument.Text));
                ok = false;
                continue;
            }
            VerbCompiledInput input;
            input.Name = argument.Text;
            input.ArgumentSlot = slot;
            input.Expected = *field;
            inputs.push_back(std::move(input));
            continue;
        }

        VerbValue value;
        const bool compiledArgument =
            argument.Source == VerbArgumentSource::Literal
                ? CompileLiteral(argument.Literal, *field, desc.Key, argument.Key, value, errors)
                : CompileReference(argument, *field, environment, desc.Key, value, errors);
        if (!compiledArgument)
        {
            ok = false;
            continue;
        }
        compiled.Constants.Set(slot, std::move(value));
    }

    for (std::size_t slot = 0; slot < root.Children.size(); ++slot)
    {
        if (filled[slot])
            continue;
        VerbValue value;
        if (!CompileDefault(root.Children[slot], desc.Key, root.Children[slot].Key, value, errors))
        {
            ok = false;
            continue;
        }
        compiled.Constants.Set(slot, std::move(value));
    }

    // Declared but unused inputs are refused rather than ignored: a producer
    // supplies values positionally, and a slot nothing reads would silently
    // shift every value after it.
    for (const std::string& declared : desc.Inputs)
    {
        const bool used = std::ranges::any_of(
            inputs, [&declared](const VerbCompiledInput& input) { return input.Name == declared; });
        if (!used)
        {
            Fail(errors, desc.Key, {},
                 std::format("input '{}' is declared but no argument reads it", declared));
            ok = false;
        }
    }

    if (!ok)
        return false;

    // Producer order, not argument order: a relay and a screen both supply
    // values in the order the binding declared its inputs.
    std::vector<VerbCompiledInput> ordered;
    ordered.reserve(inputs.size());
    for (const std::string& declared : desc.Inputs)
    {
        const auto it = std::ranges::find_if(
            inputs, [&declared](const VerbCompiledInput& input) { return input.Name == declared; });
        ordered.push_back(std::move(*it));
    }
    compiled.Inputs = std::move(ordered);

    out = std::move(compiled);
    return true;
}

bool IsVerbBindingCurrent(const CompiledVerbBinding& binding, const VerbRegistry& registry)
{
    return binding.IsValid() && binding.Catalog == registry.Catalog()
        && registry.IsLive(binding.Verb) && registry.Revision(binding.Verb) == binding.Revision;
}
