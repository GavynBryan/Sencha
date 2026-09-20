#include <authored/VerbBindingData.h>

#include <assets/data/DataAssetTypeRegistry.h>
#include <authored/VerbRegistry.h>
#include <core/identity/Id.h>
#include <core/metadata/DataSchema.h>

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // The envelope, and only the envelope. An argument map's keys are the
    // verb's argument names, which no fixed schema can enumerate -- so unknown
    // fields pass through to the compile step, which checks each one against
    // the contract it actually belongs to and says which binding and which
    // argument were wrong. That is also what preserves a key the author wrote
    // for a verb this build does not have: the document keeps it, and the
    // executable binding refuses it.
    [[nodiscard]] DataSchema MakeVerbBindingsSchema()
    {
        DataFieldSchema key;
        key.Key = "key";
        key.DisplayName = "Key";
        key.Description = "The name producers address this binding by.";
        key.Kind = DataFieldKind::String;

        DataFieldSchema verb;
        verb.Key = "verb";
        verb.DisplayName = "Verb";
        verb.Description = "The qualified name of the operation to invoke.";
        verb.Kind = DataFieldKind::String;

        DataFieldSchema inputName;
        inputName.Kind = DataFieldKind::String;

        DataFieldSchema inputs;
        inputs.Key = "inputs";
        inputs.DisplayName = "Inputs";
        inputs.Description = "Slots a producer fills, in the order it supplies them.";
        inputs.Kind = DataFieldKind::Array;
        inputs.Required = false;
        inputs.Children.push_back(std::move(inputName));

        DataFieldSchema arguments;
        arguments.Key = "arguments";
        arguments.DisplayName = "Arguments";
        arguments.Description = "One entry per argument the verb declares.";
        arguments.Kind = DataFieldKind::Record;
        arguments.Required = false;

        DataFieldSchema binding;
        binding.Kind = DataFieldKind::Record;
        binding.Children.push_back(std::move(key));
        binding.Children.push_back(std::move(verb));
        binding.Children.push_back(std::move(inputs));
        binding.Children.push_back(std::move(arguments));

        DataFieldSchema bindings;
        bindings.Key = "bindings";
        bindings.DisplayName = "Bindings";
        bindings.Kind = DataFieldKind::Array;
        bindings.Children.push_back(std::move(binding));

        DataFieldSchema root;
        root.Kind = DataFieldKind::Record;
        root.Children.push_back(std::move(bindings));

        DataSchema schema;
        schema.TypeName = std::string(kVerbBindingsTypeName);
        schema.DisplayName = "Authored bindings";
        schema.Description =
            "Named invocations of the World's authored vocabulary, addressed by key.";
        schema.Root = std::move(root);
        schema.AllowUnknownFields = true;
        return schema;
    }

    struct SourceKey
    {
        std::string_view Key;
        VerbArgumentSource Source;
    };

    // Exactly one of these per argument. Listed rather than inferred so an
    // author who wrote two of them, or none, gets told which binding it was.
    constexpr SourceKey kSourceKeys[] = {
        { "input", VerbArgumentSource::Input },
        { "const", VerbArgumentSource::Literal },
        { "asset", VerbArgumentSource::Asset },
        { "data", VerbArgumentSource::DataAsset },
        { "tag", VerbArgumentSource::Tag },
        { "entity", VerbArgumentSource::Entity },
    };

    [[nodiscard]] bool IsIdentifier(std::string_view text)
    {
        if (text.empty())
            return false;
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            const char c = text[index];
            const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
            const bool digit = c >= '0' && c <= '9';
            if (!letter && !(digit && index > 0))
                return false;
        }
        return true;
    }

    bool ParseArgument(std::string_view bindingKey,
                       const std::string& argumentKey,
                       const JsonValue& value,
                       VerbBindingArgument& out,
                       std::vector<AssetRef>& dependencies,
                       std::string& error)
    {
        if (!value.IsObject())
        {
            error = std::format(
                "binding '{}' argument '{}': expected one of {{\"const\"}}, {{\"input\"}}, "
                "{{\"asset\"}}, {{\"data\"}}, {{\"tag\"}} or {{\"entity\"}}",
                bindingKey, argumentKey);
            return false;
        }

        const JsonValue* found = nullptr;
        VerbArgumentSource source = VerbArgumentSource::Literal;
        for (const SourceKey& candidate : kSourceKeys)
        {
            const JsonValue* member = value.Find(candidate.Key);
            if (member == nullptr)
                continue;
            if (found != nullptr)
            {
                error = std::format("binding '{}' argument '{}': names more than one source",
                                    bindingKey, argumentKey);
                return false;
            }
            found = member;
            source = candidate.Source;
        }
        if (found == nullptr)
        {
            error = std::format("binding '{}' argument '{}': names no source", bindingKey,
                                argumentKey);
            return false;
        }

        out.Key = argumentKey;
        out.Source = source;

        if (source == VerbArgumentSource::Literal)
        {
            out.Literal = *found;
            return true;
        }

        if (!found->IsString())
        {
            error = std::format("binding '{}' argument '{}': this source takes a string",
                                bindingKey, argumentKey);
            return false;
        }
        out.Text = found->AsString();

        switch (source)
        {
        case VerbArgumentSource::Input:
            if (!IsIdentifier(out.Text))
            {
                error = std::format("binding '{}' argument '{}': '{}' is not an input name",
                                    bindingKey, argumentKey, out.Text);
                return false;
            }
            break;
        case VerbArgumentSource::Asset:
        case VerbArgumentSource::DataAsset:
        {
            if (!out.Text.starts_with("asset://"))
            {
                error = std::format("binding '{}' argument '{}': an asset path starts with "
                                    "'asset://'",
                                    bindingKey, argumentKey);
                return false;
            }
            // An eager dependency: the binding cannot execute without it, so the
            // asset system is told now, while it is still staging. Tags and
            // entities are not dependencies -- they are World vocabulary and
            // World contents, resolved when the binding is instantiated.
            AssetRef reference;
            reference.Type = source == VerbArgumentSource::DataAsset ? AssetType::Data
                                                                     : AssetType::Unknown;
            reference.Path = out.Text;
            dependencies.push_back(std::move(reference));
            break;
        }
        case VerbArgumentSource::Tag:
            if (out.Text.empty())
            {
                error = std::format("binding '{}' argument '{}': a gameplay tag cannot be empty",
                                    bindingKey, argumentKey);
                return false;
            }
            break;
        case VerbArgumentSource::Entity:
            if (!PersistentEntityIdFromString(out.Text).has_value())
            {
                error = std::format("binding '{}' argument '{}': expected 16 lowercase hex "
                                    "digits naming a persistent entity",
                                    bindingKey, argumentKey);
                return false;
            }
            break;
        case VerbArgumentSource::Literal:
            break;
        }
        return true;
    }

    DataAssetCompileResult CompileVerbBindings(const JsonValue& data)
    {
        DataAssetCompileResult result;

        const JsonValue* bindings = data.Find("bindings");
        if (bindings == nullptr || !bindings->IsArray())
        {
            result.Error = "$.data.bindings must be an array";
            return result;
        }

        auto library = std::make_shared<VerbBindingLibrary>();
        library->Bindings.reserve(bindings->AsArray().size());

        for (std::size_t index = 0; index < bindings->AsArray().size(); ++index)
        {
            const JsonValue& entry = bindings->AsArray()[index];
            const JsonValue* key = entry.Find("key");
            const JsonValue* verb = entry.Find("verb");
            if (key == nullptr || !key->IsString() || key->AsString().empty())
            {
                result.Error = std::format("$.data.bindings[{}] requires a non-empty key", index);
                return result;
            }
            if (verb == nullptr || !verb->IsString())
            {
                result.Error = std::format("$.data.bindings[{}] requires a verb name", index);
                return result;
            }

            VerbBindingDesc desc;
            desc.Key = key->AsString();
            desc.KeyId = MakeVerbBindingKey(desc.Key);
            desc.VerbName = verb->AsString();

            if (library->Find(desc.Key) != nullptr)
            {
                result.Error =
                    std::format("binding key '{}' appears more than once", desc.Key);
                return result;
            }
            // Checked here rather than at instantiation because it is a fact
            // about the file: a World that has never heard of the verb should
            // still report a malformed name as a malformed name.
            if (!IsValidVerbName(desc.VerbName))
            {
                result.Error = std::format("binding '{}': '{}' is not a verb name", desc.Key,
                                           desc.VerbName);
                return result;
            }

            if (const JsonValue* inputs = entry.Find("inputs"); inputs != nullptr)
            {
                if (!inputs->IsArray())
                {
                    result.Error = std::format("binding '{}': inputs must be an array", desc.Key);
                    return result;
                }
                for (const JsonValue& input : inputs->AsArray())
                {
                    if (!input.IsString() || !IsIdentifier(input.AsString()))
                    {
                        result.Error =
                            std::format("binding '{}': every input is an identifier", desc.Key);
                        return result;
                    }
                    if (std::ranges::find(desc.Inputs, input.AsString()) != desc.Inputs.end())
                    {
                        result.Error = std::format("binding '{}': input '{}' is declared twice",
                                                   desc.Key, input.AsString());
                        return result;
                    }
                    desc.Inputs.push_back(input.AsString());
                }
            }

            if (const JsonValue* arguments = entry.Find("arguments"); arguments != nullptr)
            {
                if (!arguments->IsObject())
                {
                    result.Error =
                        std::format("binding '{}': arguments must be an object", desc.Key);
                    return result;
                }
                for (const auto& [argumentKey, argumentValue] : arguments->AsObject())
                {
                    VerbBindingArgument argument;
                    if (!ParseArgument(desc.Key, argumentKey, argumentValue, argument,
                                       result.Dependencies, result.Error))
                    {
                        return result;
                    }
                    desc.Arguments.push_back(std::move(argument));
                }
            }

            library->Bindings.push_back(std::move(desc));
        }

        result.Value = std::move(library);
        return result;
    }
}

void RegisterVerbBindingData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    DataAssetTypeRegistration type;
    type.Name = std::string(kVerbBindingsTypeName);
    type.CurrentVersion = 1;
    type.Compile = CompileVerbBindings;
    if (!types.Register(std::move(type)))
        return;

    if (!schemas.Register(MakeVerbBindingsSchema()))
        (void)types.Unregister(kVerbBindingsTypeName);
}

void UnregisterVerbBindingData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (types.Unregister(kVerbBindingsTypeName))
        (void)schemas.Unregister(kVerbBindingsTypeName);
}
