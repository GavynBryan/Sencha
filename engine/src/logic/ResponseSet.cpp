#include <logic/ResponseSet.h>

#include <authored/AuthoredEventRegistry.h>
#include <authored/WorldVocabulary.h>
#include <core/hash/Fnv1a.h>
#include <core/json/JsonStringify.h>
#include <core/metadata/DataSchema.h>
#include <ecs/World.h>
#include <logic/Responses.h>
#include <logic/VerbSequence.h>

#include <algorithm>
#include <format>
#include <string_view>

namespace
{
std::uint64_t HashText(std::string_view text)
{
    std::uint64_t hash = kFnv1aOffsetBasis;
    HashFnv1aBytes(hash, text.data(), text.size());
    return hash != 0 ? hash : 1;
}

bool ReadScope(const JsonValue& value, ResponseCooldownScope& out)
{
    const std::string text = value.IsString() ? value.AsString() : std::string();
    if (text == "binding")
        out = ResponseCooldownScope::Binding;
    else if (text == "instigator")
        out = ResponseCooldownScope::Instigator;
    else
        return false;
    return true;
}

// One binding's members; a malformed member is reported and its default kept,
// so the rest of the binding still reads.
void ReadBinding(const std::string& key, const JsonValue& authored, VerbSequenceStore& sequences,
                 ResponseBindingDesc& out, std::vector<std::string>& errors)
{
    const std::string where = std::format("binding '{}'", key);
    out.Key = key;
    out.KeyHash = ResponseKeyHash(key);
    out.DefinitionHash = HashText(JsonStringify(authored));
    if (!authored.IsObject())
    {
        errors.push_back(std::format("{}: a binding is an object", where));
        return;
    }

    JsonValue sequence = EmptyVerbSequence();
    for (const auto& [member, value] : authored.AsObject())
    {
        if (member == "on")
        {
            if (value.IsString() && !value.AsString().empty())
                out.On = value.AsString();
            else
                errors.push_back(std::format("{}: 'on' names a declared event", where));
        }
        else if (member == "enabled" || member == "once")
        {
            if (!value.IsBool())
                errors.push_back(std::format("{}: '{}' is true or false", where, member));
            else if (member == "enabled")
                out.StartsEnabled = value.AsBool();
            else
                out.Once = value.AsBool();
        }
        else if (member == "cooldown")
        {
            if (value.IsNumber() && value.AsNumber() >= 0.0)
                out.CooldownSeconds = static_cast<float>(value.AsNumber());
            else
                errors.push_back(std::format("{}: 'cooldown' is seconds, zero or more", where));
        }
        else if (member == "cooldown_scope")
        {
            if (!ReadScope(value, out.CooldownScope))
                errors.push_back(std::format("{}: 'cooldown_scope' is \"binding\" or \"instigator\"", where));
        }
        else if (member == "sequence")
        {
            sequence = value;
        }
        else
        {
            errors.push_back(std::format("{}: unknown member '{}'", where, member));
        }
    }
    if (authored.Find("on") == nullptr)
        errors.push_back(std::format("{}: 'on' names the declared event it answers", where));
    out.Sequence = sequences.Intern(sequence, out.On);
}
} // namespace

std::uint64_t ResponseKeyHash(std::string_view key)
{
    return HashText(key);
}

int FindResponseBinding(std::span<const ResponseBindingDesc> bindings, std::uint64_t keyHash)
{
    const auto found = std::ranges::find(bindings, keyHash, &ResponseBindingDesc::KeyHash);
    return found != bindings.end() ? static_cast<int>(found - bindings.begin()) : -1;
}

ResponseSetKey ResponseSetStore::Intern(const JsonValue& bindings, VerbSequenceStore& sequences)
{
    std::string text = JsonStringify(bindings);
    ResponseSetKey key{ HashText(text) };
    // Two texts hashing alike is refused rather than answered with the wrong one.
    while (true)
    {
        const auto found = Entries.find(key);
        if (found == Entries.end())
            break;
        if (found->second.Text == text)
            return key;
        key = ResponseSetKey{ key.Value + 1 != 0 ? key.Value + 1 : 1 };
    }

    Entry& entry = Entries[key];
    entry.Text = std::move(text);
    entry.Source = bindings;
    if (!bindings.IsObject())
    {
        entry.ReadErrors.push_back("bindings are an object of keyed bindings: {\"<key>\": {\"on\": ...}}");
        return key;
    }

    std::vector<const std::pair<std::string, JsonValue>*> members;
    for (const auto& member : bindings.AsObject())
        members.push_back(&member);
    std::ranges::sort(members, {}, [](const auto* member) { return std::string_view(member->first); });
    for (const auto* member : members)
    {
        if (!IsValidStableKey(member->first))
        {
            entry.ReadErrors.push_back(std::format("binding '{}': a key is up to 64 letters, digits, '_' or '-'",
                                                   member->first));
            continue;
        }
        if (!entry.Bindings.empty() && entry.Bindings.back().Key == member->first)
        {
            entry.ReadErrors.push_back(std::format("binding '{}' is keyed twice; the first is kept", member->first));
            continue;
        }
        if (entry.Bindings.size() == kResponseBindingCapacity)
        {
            entry.ReadErrors.push_back(std::format("more than {} bindings; '{}' and those after it are left out",
                                                   kResponseBindingCapacity, member->first));
            break;
        }
        ResponseBindingDesc binding;
        ReadBinding(member->first, member->second, sequences, binding, entry.ReadErrors);
        if (!binding.StartsEnabled)
            entry.StartsDisabled |= 1u << entry.Bindings.size();
        entry.Bindings.push_back(std::move(binding));
    }
    return key;
}

const JsonValue* ResponseSetStore::Source(ResponseSetKey set) const
{
    const auto found = Entries.find(set);
    return found != Entries.end() ? &found->second.Source : nullptr;
}

std::span<const ResponseBindingDesc> ResponseSetStore::Bindings(ResponseSetKey set) const
{
    const auto found = Entries.find(set);
    return found != Entries.end() ? std::span<const ResponseBindingDesc>(found->second.Bindings)
                                  : std::span<const ResponseBindingDesc>();
}

std::uint32_t ResponseSetStore::StartsDisabled(ResponseSetKey set) const
{
    const auto found = Entries.find(set);
    return found != Entries.end() ? found->second.StartsDisabled : 0;
}

void ResponseSetStore::Revalidate(const World& world)
{
    const AuthoredEventRegistry* events = FindAuthoredEventRegistry(world);
    Current = events != nullptr ? CatalogStamp::Of(*events) : CatalogStamp{};
    Revalidated = true;
}

void ResponseSetStore::Compile(Entry& entry, const World& world)
{
    const AuthoredEventRegistry* events = FindAuthoredEventRegistry(world);
    entry.Diagnostics = entry.ReadErrors;
    entry.Compiled.assign(entry.Bindings.size(), CompiledResponseBinding{});
    for (std::size_t i = 0; i < entry.Bindings.size(); ++i)
    {
        const ResponseBindingDesc& binding = entry.Bindings[i];
        if (binding.On.empty())
            continue;
        const AuthoredEventId event = events != nullptr ? events->Find(binding.On) : AuthoredEventId{};
        const AuthoredEventDefinition* definition = event.IsValid() ? events->Get(event) : nullptr;
        if (definition == nullptr)
        {
            entry.Diagnostics.push_back(std::format("binding '{}': no event '{}'", binding.Key, binding.On));
            continue;
        }
        CompiledResponseBinding& compiled = entry.Compiled[i];
        compiled.Event = event;
        compiled.Revision = events->Revision(event);
        const std::vector<DataFieldSchema>& payload = definition->Payload.Children;
        const auto instigator = std::ranges::find(payload, definition->Instigator, &DataFieldSchema::Key);
        if (!definition->Instigator.empty() && instigator != payload.end())
            compiled.InstigatorPort = static_cast<std::uint8_t>(kResponsePorts.size() + (instigator - payload.begin()));
        if (binding.CooldownScope == ResponseCooldownScope::Instigator && definition->Instigator.empty())
            entry.Diagnostics.push_back(std::format("binding '{}': '{}' names no instigator, so its cooldown is "
                                                    "the whole binding's",
                                                    binding.Key, binding.On));
    }
    entry.CompiledAgainst = Current;
    entry.Built = true;
}

ResponseSetView ResponseSetStore::Resolve(ResponseSetKey set, const World& world)
{
    const auto found = Entries.find(set);
    if (found == Entries.end())
        return {};
    if (!Revalidated)
        Revalidate(world);
    Entry& entry = found->second;
    if (!entry.Built || !(entry.CompiledAgainst == Current))
        Compile(entry, world);
    return ResponseSetView{ entry.Bindings, entry.Compiled };
}

std::span<const std::string> ResponseSetStore::Diagnostics(ResponseSetKey set) const
{
    const auto found = Entries.find(set);
    if (found == Entries.end())
        return {};
    const Entry& entry = found->second;
    return entry.Built ? std::span<const std::string>(entry.Diagnostics)
                       : std::span<const std::string>(entry.ReadErrors);
}

ResponseDefinitions& ResponseDefinitionsOf(World& world)
{
    ResponseDefinitions* definitions = world.TryGetResource<ResponseDefinitions>();
    return definitions != nullptr ? *definitions : world.AddResource<ResponseDefinitions>();
}

JsonValue EmptyResponseBindings()
{
    return JsonValue(JsonValue::Object{});
}

bool SetResponseBindings(World& world, EntityId entity, const JsonValue& bindings)
{
    Responses* responses = world.IsRegistered<Responses>() ? world.TryGet<Responses>(entity) : nullptr;
    if (responses == nullptr)
        return false;
    ResponseDefinitions& definitions = ResponseDefinitionsOf(world);
    const ResponseSetKey next = definitions.Sets.Intern(bindings, definitions.Sequences);
    if (next == responses->Set)
        return true;

    // State follows the key, and only while the binding's text is unchanged.
    const std::span<const ResponseBindingDesc> before = definitions.Sets.Bindings(responses->Set);
    const std::span<const ResponseBindingDesc> after = definitions.Sets.Bindings(next);
    std::uint32_t disabled = definitions.Sets.StartsDisabled(next);
    std::uint32_t consumed = 0;
    for (std::size_t i = 0; i < after.size(); ++i)
    {
        const int was = FindResponseBinding(before, after[i].KeyHash);
        if (was < 0 || before[static_cast<std::size_t>(was)].DefinitionHash != after[i].DefinitionHash)
            continue;
        const std::uint32_t from = 1u << static_cast<std::uint32_t>(was);
        const std::uint32_t to = 1u << i;
        disabled = (responses->Disabled & from) != 0 ? (disabled | to) : (disabled & ~to);
        consumed |= (responses->Consumed & from) != 0 ? to : 0;
    }
    responses->Set = next;
    responses->Disabled = disabled;
    responses->Consumed = consumed;
    return true;
}

const JsonValue* ResponseBindingsSource(const World& world, EntityId entity)
{
    const Responses* responses = world.IsRegistered<Responses>() ? world.TryGet<Responses>(entity) : nullptr;
    const ResponseDefinitions* definitions = world.TryGetResource<ResponseDefinitions>();
    return responses != nullptr && definitions != nullptr ? definitions->Sets.Source(responses->Set) : nullptr;
}
