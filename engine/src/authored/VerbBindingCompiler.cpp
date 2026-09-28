#include <authored/VerbBindingCompiler.h>

#include <authored/AuthoredLiteral.h>

#include <assets/data/DataAssetCache.h>
#include <core/assets/AssetRegistry.h>
#include <core/identity/Id.h>
#include <gameplay_tags/GameplayTagRegistry.h>

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

    [[nodiscard]] std::string BindingSubject(std::string_view bindingKey)
    {
        return std::format("binding '{}'", bindingKey);
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
                          AuthoredValue& out,
                          bool& referencesChecked,
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

            // And the asset, when the host can say, has to be of that kind. A
            // schema that constrains a kind or a subtype is describing what the
            // operation will lease; a reference that passed here and failed
            // there would fail in the wrong place, with the wrong diagnostic.
            if (environment.Assets == nullptr)
                referencesChecked = false;
            else
            {
                const AssetRecord* record = environment.Assets->FindByPath(argument.Text);
                if (record == nullptr)
                {
                    Fail(errors, bindingKey, argument.Key,
                         std::format("no asset at '{}'", argument.Text));
                    return false;
                }
                if (reference.Type != AssetType::Unknown && record->Type != reference.Type)
                {
                    Fail(errors, bindingKey, argument.Key,
                         std::format("'{}' is a {} asset; the verb wants {}", argument.Text,
                                     AssetTypeToString(record->Type),
                                     AssetTypeToString(reference.Type)));
                    return false;
                }
            }
            // A subtype is checked against the resident value when the host
            // can read one. A host that gives no cache is unchecked, like one
            // that gives no registry; a host that gives one and has not made
            // the asset resident has not preloaded the binding's dependencies,
            // which is the fault reported.
            if (wantsData && !expected.Reference.DataSubtype.empty()
                && environment.DataAssets == nullptr)
                referencesChecked = false;
            if (wantsData && !expected.Reference.DataSubtype.empty()
                && environment.DataAssets != nullptr)
            {
                const DataAssetHandle resident = environment.DataAssets->Find(argument.Text);
                if (!resident.IsValid())
                {
                    Fail(errors, bindingKey, argument.Key,
                         std::format("'{}' is not resident, so its subtype cannot be checked "
                                     "against '{}'",
                                     argument.Text, expected.Reference.DataSubtype));
                    return false;
                }
                const std::string_view subtype = environment.DataAssets->GetSubtype(resident);
                if (subtype != expected.Reference.DataSubtype)
                {
                    Fail(errors, bindingKey, argument.Key,
                         std::format("'{}' is a '{}' data asset; the verb wants '{}'",
                                     argument.Text, subtype, expected.Reference.DataSubtype));
                    return false;
                }
            }
            out = wantsData ? AuthoredValue::DataAsset(std::move(reference))
                            : AuthoredValue::Asset(std::move(reference));
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
            out = AuthoredValue::Tag(tag);
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
            // Not resolved here. The identity is the authored relationship;
            // the handle is whatever entity carries it at the moment of each
            // invocation, which is the dispatcher's to look up.
            out = AuthoredValue::PersistentEntity(*identity);
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
    compiled.Constants = AuthoredArguments(root.Children.size());

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
            // One input, as many destinations as name it. The producer's one
            // value is checked against each at invocation.
            auto existing = std::ranges::find_if(
                inputs, [&argument](const VerbCompiledInput& input) {
                    return input.Name == argument.Text;
                });
            if (existing == inputs.end())
            {
                VerbCompiledInput input;
                input.Name = argument.Text;
                inputs.push_back(std::move(input));
                existing = inputs.end() - 1;
            }
            existing->Destinations.push_back(VerbInputDestination{ slot, *field });
            continue;
        }

        AuthoredValue value;
        const bool compiledArgument =
            argument.Source == VerbArgumentSource::Literal
                ? CompileAuthoredLiteral(argument.Literal, *field, BindingSubject(desc.Key),
                                         argument.Key, value, errors)
                : CompileReference(argument, *field, environment, desc.Key, value,
                                   compiled.ReferencesChecked, errors);
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
        AuthoredValue value;
        if (!CompileAuthoredDefault(root.Children[slot], BindingSubject(desc.Key),
                                    root.Children[slot].Key, value, errors))
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
