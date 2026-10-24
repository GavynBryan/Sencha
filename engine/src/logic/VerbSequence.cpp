#include <logic/VerbSequence.h>

#include <authored/VerbBindingData.h>

#include <algorithm>
#include <format>
#include <initializer_list>
#include <string_view>

namespace
{
constexpr std::string_view kConstantKeys[] = { "const", "tag", "data", "asset", "entity" };

bool OnlyMembers(const JsonValue& object, std::initializer_list<std::string_view> allowed, std::string_view where,
                 std::vector<std::string>& errors, bool constant = false)
{
    bool ok = true;
    for (const auto& [key, value] : object.AsObject())
    {
        const bool known = std::ranges::find(allowed, std::string_view(key)) != allowed.end()
            || (constant && std::ranges::find(kConstantKeys, std::string_view(key)) != std::end(kConstantKeys));
        if (!known)
        {
            errors.push_back(std::format("{}: unknown member '{}'", where, key));
            ok = false;
        }
    }
    return ok;
}

std::string TextMember(const JsonValue& object, std::string_view key)
{
    const JsonValue* member = object.Find(key);
    return member != nullptr && member->IsString() ? member->AsString() : std::string();
}

bool ReadArguments(const JsonValue* authored, std::string_view where, std::vector<SequenceArgumentDesc>& out,
                   std::vector<AssetRef>& dependencies, std::vector<std::string>& errors);

bool ReadModifiers(const JsonValue& object, std::string_view where, SequenceValueDesc& out,
                   std::vector<std::string>& errors)
{
    if (const JsonValue* frame = object.Find("frame"))
    {
        if (!frame->IsString() || frame->AsString().empty())
        {
            errors.push_back(std::format("{}: 'frame' names an entity port", where));
            return false;
        }
        out.Frame = frame->AsString();
    }
    if (const JsonValue* scale = object.Find("scale"))
    {
        if (!scale->IsNumber())
        {
            errors.push_back(std::format("{}: 'scale' is a number", where));
            return false;
        }
        out.Scale = scale->AsNumber();
    }
    return true;
}

bool ReadValue(const JsonValue& authored, std::string_view where, SequenceValueDesc& out,
               std::vector<AssetRef>& dependencies, std::vector<std::string>& errors)
{
    if (authored.IsNumber() || authored.IsBool())
    {
        out.Source = SequenceValueSource::Constant;
        out.Constant.Source = VerbArgumentSource::Literal;
        out.Constant.Literal = authored;
        return true;
    }
    if (!authored.IsObject())
    {
        errors.push_back(std::format("{}: a value is a number, a bool, or one of {{const}}, {{tag}}, {{data}}, "
                                     "{{asset}}, {{entity}}, {{port}}, {{query}}",
                                     where));
        return false;
    }
    if (!ReadModifiers(authored, where, out, errors))
        return false;

    if (const JsonValue* port = authored.Find("port"))
    {
        if (!OnlyMembers(authored, { "port", "frame", "scale" }, where, errors))
            return false;
        if (!port->IsString() || port->AsString().empty())
        {
            errors.push_back(std::format("{}: 'port' names a port", where));
            return false;
        }
        out.Source = SequenceValueSource::Port;
        out.Name = port->AsString();
        return true;
    }
    if (const JsonValue* query = authored.Find("query"))
    {
        if (!OnlyMembers(authored, { "query", "arguments", "member", "frame", "scale" }, where, errors))
            return false;
        if (!query->IsString() || query->AsString().empty())
        {
            errors.push_back(std::format("{}: 'query' names a query", where));
            return false;
        }
        out.Source = SequenceValueSource::Query;
        out.Name = query->AsString();
        out.Member = TextMember(authored, "member");
        return ReadArguments(authored.Find("arguments"), std::format("{} '{}'", where, out.Name), out.Arguments,
                             dependencies, errors);
    }

    if (!OnlyMembers(authored, { "frame", "scale" }, where, errors, true))
        return false;
    std::string error;
    if (!ParseVerbBindingArgument(where, "value", authored, out.Constant, dependencies, error))
    {
        errors.push_back(std::move(error));
        return false;
    }
    if (out.Constant.Source == VerbArgumentSource::Input)
    {
        errors.push_back(std::format("{}: a sequence reads a {{port}}, not an {{input}}", where));
        return false;
    }
    out.Source = SequenceValueSource::Constant;
    return true;
}

bool ReadArguments(const JsonValue* authored, std::string_view where, std::vector<SequenceArgumentDesc>& out,
                   std::vector<AssetRef>& dependencies, std::vector<std::string>& errors)
{
    if (authored == nullptr)
        return true;
    if (!authored->IsObject())
    {
        errors.push_back(std::format("{}: 'arguments' is an object", where));
        return false;
    }
    bool ok = true;
    for (const auto& [key, value] : authored->AsObject())
    {
        SequenceArgumentDesc argument;
        argument.Key = key;
        if (ReadValue(value, std::format("{} argument '{}'", where, key), argument.Value, dependencies, errors))
            out.push_back(std::move(argument));
        else
            ok = false;
    }
    return ok;
}

bool ReadNames(const JsonValue* authored, std::string_view where, std::vector<std::string>& out,
               std::vector<std::string>& errors)
{
    if (authored == nullptr)
        return true;
    if (!authored->IsArray())
    {
        errors.push_back(std::format("{}: a list of names", where));
        return false;
    }
    for (const JsonValue& name : authored->AsArray())
    {
        if (!name.IsString() || name.AsString().empty())
        {
            errors.push_back(std::format("{}: a list of names", where));
            return false;
        }
        out.push_back(name.AsString());
    }
    return true;
}

bool ReadCondition(const JsonValue& authored, std::string_view where, SequenceConditionDesc& out,
                   std::vector<AssetRef>& dependencies, std::vector<std::string>& errors)
{
    if (!authored.IsObject())
    {
        errors.push_back(std::format("{}: a condition is an object", where));
        return false;
    }
    if (const JsonValue* port = authored.Find("entity"))
    {
        if (!OnlyMembers(authored, { "entity", "has", "tags" }, where, errors))
            return false;
        out.Kind = SequenceConditionKind::Entity;
        out.Port = port->IsString() ? port->AsString() : std::string();
        if (out.Port.empty())
        {
            errors.push_back(std::format("{}: 'entity' names an entity port", where));
            return false;
        }
        bool ok = ReadNames(authored.Find("has"), std::format("{} 'has'", where), out.Components, errors);
        if (const JsonValue* tags = authored.Find("tags"))
        {
            if (!tags->IsObject())
            {
                errors.push_back(std::format("{}: 'tags' is {{all, any, none}}", where));
                return false;
            }
            ok = ReadNames(tags->Find("all"), std::format("{} tags 'all'", where), out.Tags.All, errors) && ok;
            ok = ReadNames(tags->Find("any"), std::format("{} tags 'any'", where), out.Tags.Any, errors) && ok;
            ok = ReadNames(tags->Find("none"), std::format("{} tags 'none'", where), out.Tags.None, errors) && ok;
        }
        return ok;
    }
    if (const JsonValue* query = authored.Find("query"))
    {
        if (!OnlyMembers(authored, { "query", "arguments", "expect" }, where, errors))
            return false;
        out.Kind = SequenceConditionKind::Query;
        out.Query = query->IsString() ? query->AsString() : std::string();
        if (out.Query.empty())
        {
            errors.push_back(std::format("{}: 'query' names a query", where));
            return false;
        }
        if (const JsonValue* expect = authored.Find("expect"))
        {
            if (!expect->IsBool())
            {
                errors.push_back(std::format("{}: 'expect' is true or false", where));
                return false;
            }
            out.Expect = expect->AsBool();
        }
        return ReadArguments(authored.Find("arguments"), std::format("{} '{}'", where, out.Query), out.Arguments,
                             dependencies, errors);
    }
    errors.push_back(std::format("{}: a condition is {{entity}} or {{query}}", where));
    return false;
}

bool ReadCommand(const JsonValue& authored, std::string_view where, SequenceCommandDesc& out,
                 std::vector<AssetRef>& dependencies, std::vector<std::string>& errors)
{
    if (!authored.IsObject())
    {
        errors.push_back(std::format("{}: a command is an object", where));
        return false;
    }
    if (!OnlyMembers(authored, { "verb", "arguments", "on_failure" }, where, errors))
        return false;
    out.Verb = TextMember(authored, "verb");
    if (out.Verb.empty())
    {
        errors.push_back(std::format("{}: 'verb' names a verb", where));
        return false;
    }
    if (const JsonValue* policy = authored.Find("on_failure"))
    {
        const std::string text = policy->IsString() ? policy->AsString() : std::string();
        if (text == "stop")
            out.OnFailure = SequenceFailurePolicy::Stop;
        else if (text != "continue")
        {
            errors.push_back(std::format("{}: 'on_failure' is \"continue\" or \"stop\"", where));
            return false;
        }
    }
    return ReadArguments(authored.Find("arguments"), std::format("{} '{}'", where, out.Verb), out.Arguments,
                         dependencies, errors);
}

template<typename Desc, typename Read>
bool ReadList(const JsonValue& authored, std::string_view key, std::string_view noun, std::vector<Desc>& out,
              std::vector<AssetRef>& dependencies, std::vector<std::string>& errors, Read read)
{
    const JsonValue* list = authored.Find(key);
    if (list == nullptr)
        return true;
    if (!list->IsArray())
    {
        errors.push_back(std::format("'{}' is a list", key));
        return false;
    }
    bool ok = true;
    for (std::size_t i = 0; i < list->AsArray().size(); ++i)
    {
        Desc element;
        if (read(list->AsArray()[i], std::format("{} {}", noun, i + 1), element, dependencies, errors))
            out.push_back(std::move(element));
        else
            ok = false;
    }
    return ok;
}
} // namespace

bool ReadVerbSequence(const JsonValue& authored,
                      VerbSequenceDesc& out,
                      std::vector<AssetRef>& dependencies,
                      std::vector<std::string>& errors)
{
    if (!authored.IsObject())
    {
        errors.push_back("a sequence is {\"conditions\": [...], \"commands\": [...]}");
        return false;
    }
    bool ok = OnlyMembers(authored, { "conditions", "commands" }, "sequence", errors);
    ok = ReadList(authored, "conditions", "condition", out.Conditions, dependencies, errors, ReadCondition) && ok;
    ok = ReadList(authored, "commands", "command", out.Commands, dependencies, errors, ReadCommand) && ok;
    return ok;
}

JsonValue EmptyVerbSequence()
{
    return JsonValue(JsonValue::Object{ { "conditions", JsonValue(JsonValue::Array{}) },
                                        { "commands", JsonValue(JsonValue::Array{}) } });
}
