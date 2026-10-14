#include <abilities/CuePresentationData.h>

#include <authored/VerbBindingData.h>

#include <algorithm>
#include <format>
#include <memory>

namespace
{
std::string Text(const JsonValue& object, std::string_view member)
{
    const JsonValue* value = object.IsObject() ? object.Find(member) : nullptr;
    return value != nullptr && value->IsString() ? value->AsString() : std::string{};
}

bool Binding(const JsonValue& entry, std::string_view kind, std::string key, VerbBindingDesc& out,
             std::vector<AssetRef>& dependencies, std::vector<std::string>& errors)
{
    out.VerbName = Text(entry, kind);
    if (out.VerbName.empty())
    {
        errors.push_back(std::format("{}: names no {}", key, kind));
        return false;
    }
    out.Key = std::move(key);
    out.KeyId = MakeVerbBindingKey(out.Key);
    const JsonValue* arguments = entry.Find("arguments");
    if (arguments == nullptr)
        return true;
    if (!arguments->IsObject())
    {
        errors.push_back(std::format("{}: arguments are an object", out.Key));
        return false;
    }
    for (const auto& [name, value] : arguments->AsObject())
    {
        VerbBindingArgument argument;
        argument.Key = name;
        if (const std::string port = Text(value, "port"); !port.empty())
        {
            if (std::ranges::find(kCuePresentationPorts, port) == kCuePresentationPorts.end())
            {
                errors.push_back(std::format("{}.{}: '{}' is not a presentation port", out.Key, name, port));
                return false;
            }
            argument.Source = VerbArgumentSource::Input;
            argument.Text = port;
            if (std::ranges::find(out.Inputs, port) == out.Inputs.end())
                out.Inputs.push_back(port);
        }
        else
        {
            std::string error;
            if (!ParseVerbBindingArgument(out.Key, name, value, argument, dependencies, error))
            {
                errors.push_back(std::move(error));
                return false;
            }
        }
        out.Arguments.push_back(std::move(argument));
    }
    return true;
}

bool Bindings(const JsonValue& cue, std::string_view list, std::string_view kind, const std::string& key,
              std::vector<VerbBindingDesc>& out, std::vector<AssetRef>& dependencies, std::vector<std::string>& errors)
{
    const JsonValue* entries = cue.Find(list);
    if (entries == nullptr)
        return true;
    if (!entries->IsArray())
    {
        errors.push_back(std::format("{}.{}: a list", key, list));
        return false;
    }
    bool ok = true;
    for (std::size_t i = 0; i < entries->AsArray().size(); ++i)
        ok = Binding(entries->AsArray()[i], kind, std::format("{}.{}[{}]", key, list, i), out.emplace_back(),
                     dependencies, errors)
            && ok;
    return ok;
}

DataFieldSchema Field(DataFieldKind kind, std::string key, std::string label, std::string summary = {})
{
    DataFieldSchema field = MakeDataField(kind, std::move(key), std::move(label), std::move(summary));
    field.Required = false;
    return field;
}

DataSchema MakeSchema()
{
    DataFieldSchema cue = MakeDataField(DataFieldKind::Record, "cue", "Cue");
    cue.Children.push_back(MakeDataField(DataFieldKind::StableKey, "key", "Key"));
    cue.Children.push_back(MakeDataField(DataFieldKind::GameplayTag, "cue", "Cue", "The cue an ability makes."));
    DataFieldSchema once = Field(DataFieldKind::Array, "once", "Once", "Cosmetic verbs run for each occurrence.");
    once.Children.push_back(Field(DataFieldKind::Record, "verb", "Verb"));
    DataFieldSchema sustain = Field(DataFieldKind::Array, "sustain", "Sustained", "Cosmetic leases held while a phase presents it.");
    sustain.Children.push_back(Field(DataFieldKind::Record, "lease", "Lease"));
    cue.Children.push_back(std::move(once));
    cue.Children.push_back(std::move(sustain));
    DataFieldSchema cues = MakeDataField(DataFieldKind::Array, "cues", "Cues");
    cues.Editor.TitleKey = "cue";
    cues.Children.push_back(std::move(cue));

    DataSchema schema;
    schema.TypeName = std::string(kCuePresentationType);
    schema.DisplayName = "Cue presentation";
    schema.Description = "How each cue an ability makes is presented: sounds, reactions, and whatever a game registers.";
    schema.AllowUnknownFields = true;
    schema.Root.Kind = DataFieldKind::Record;
    schema.Root.Children = { std::move(cues) };
    return schema;
}

DataAssetCompileResult Compile(const JsonValue& data)
{
    auto declaration = std::make_shared<CuePresentationDeclaration>();
    std::vector<AssetRef> dependencies;
    std::vector<std::string> errors;
    if (!CompileCuePresentation(data, *declaration, dependencies, errors))
        return { {}, {}, errors.front() };
    return { std::move(declaration), std::move(dependencies), {} };
}
} // namespace

bool CompileCuePresentation(const JsonValue& data, CuePresentationDeclaration& out, std::vector<AssetRef>& dependencies,
                            std::vector<std::string>& errors)
{
    const std::size_t before = errors.size();
    const JsonValue* cues = data.IsObject() ? data.Find("cues") : nullptr;
    if (cues == nullptr || !cues->IsArray())
    {
        errors.push_back("$.data.cues: a list of cues");
        return false;
    }
    for (const JsonValue& cue : cues->AsArray())
    {
        CuePresentationEntry entry;
        entry.Key = Text(cue, "key");
        entry.Cue = Text(cue, "cue");
        if (entry.Key.empty() || entry.Cue.empty())
        {
            errors.push_back("$.data.cues: each names its key and its cue");
            continue;
        }
        (void)Bindings(cue, "once", "verb", entry.Key, entry.Once, dependencies, errors);
        (void)Bindings(cue, "sustain", "lease", entry.Key, entry.Sustain, dependencies, errors);
        out.Entries.push_back(std::move(entry));
    }
    return errors.size() == before;
}

void RegisterCuePresentationData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kCuePresentationType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kCuePresentationType);
}
