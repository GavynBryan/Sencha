#include <logic/VerbSequenceCompiler.h>

#include <authored/AuthoredArgumentMatch.h>
#include <authored/AuthoredLiteral.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbRegistry.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <format>

namespace
{
const DataFieldSchema& Unwrapped(const DataFieldSchema& field)
{
    return field.Kind == DataFieldKind::Optional && field.Children.size() == 1 ? field.Children.front() : field;
}

bool IsVector3(const DataFieldSchema& field)
{
    return field.Kind == DataFieldKind::Vector && field.VectorLength == 3;
}

bool SameShape(const DataFieldSchema& produced, const DataFieldSchema& expected)
{
    const DataFieldSchema& a = Unwrapped(produced);
    const DataFieldSchema& b = Unwrapped(expected);
    return a.Kind == b.Kind && (a.Kind != DataFieldKind::Vector || a.VectorLength == b.VectorLength);
}

class SequenceCompilation
{
public:
    SequenceCompilation(const VerbSequenceEnvironment& environment, std::vector<std::string>& diagnostics)
        : Env(environment)
        , Diagnostics(diagnostics)
    {
    }

    CompiledSequenceCondition Condition(const SequenceConditionDesc& desc, std::size_t index)
    {
        const std::string where = std::format("condition {}", index + 1);
        CompiledSequenceCondition out;
        out.Kind = desc.Kind;
        out.Expect = desc.Expect;
        bool ok = true;
        if (desc.Kind == SequenceConditionKind::Entity)
        {
            ok = EntityPort(desc.Port, where, out.Port);
            for (const std::string& identity : desc.Components)
            {
                const ComponentId component = Env.Components != nullptr
                    ? Env.Components->GetComponentIdByType(MakeComponentTypeId(identity))
                    : InvalidComponentId;
                if (component == InvalidComponentId)
                {
                    Report(where, std::format("no component '{}'", identity));
                    ok = false;
                    continue;
                }
                out.Components.push_back(component);
            }
            if (!desc.Tags.Empty())
            {
                std::vector<std::string> missing;
                if (Env.Bindings.Tags != nullptr)
                    out.Tags = ResolveTagQuery(desc.Tags, *Env.Bindings.Tags, missing);
                else
                    Report(where, "this World has no gameplay tags");
                for (const std::string& tag : missing)
                    Report(where, std::format("no gameplay tag '{}'", tag));
                ok = ok && Env.Bindings.Tags != nullptr && missing.empty();
                out.HasTags = true;
            }
        }
        else
        {
            const AuthoredQueryDefinition* query = FindQuery(desc.Query, where, out.Query);
            ok = query != nullptr;
            if (query != nullptr)
            {
                const DataFieldSchema& result = Unwrapped(query->Result);
                if (result.Kind != DataFieldKind::Bool)
                {
                    Report(where, std::format("'{}' answers no true or false", desc.Query));
                    ok = false;
                }
                ok = QueryArguments(*query, desc.Arguments, where, out.Arguments) && ok;
            }
        }
        out.Resolved = ok;
        return out;
    }

    CompiledSequenceCommand Command(const SequenceCommandDesc& desc, std::size_t index)
    {
        const std::string where = std::format("command {} '{}'", index + 1, desc.Verb);
        CompiledSequenceCommand out;
        out.Verb = desc.Verb;
        out.OnFailure = desc.OnFailure;
        if (Env.Bindings.Verbs == nullptr)
        {
            Report(where, "this World declares no verbs");
            return out;
        }

        VerbBindingDesc binding;
        binding.Key = std::format("sequence.command.{}", index + 1);
        binding.KeyId = MakeVerbBindingKey(binding.Key);
        binding.VerbName = desc.Verb;
        for (const SequenceArgumentDesc& argument : desc.Arguments)
        {
            VerbBindingArgument bound;
            if (argument.Value.IsPlainConstant())
            {
                bound = argument.Value.Constant;
            }
            else
            {
                bound.Source = VerbArgumentSource::Input;
                bound.Text = argument.Key;
                binding.Inputs.push_back(argument.Key);
            }
            bound.Key = argument.Key;
            binding.Arguments.push_back(std::move(bound));
        }

        std::vector<std::string> errors;
        bool ok = CompileVerbBinding(binding, Env.Bindings, out.Binding, errors);
        for (std::string& error : errors)
            Report(where, std::move(error));
        if (ok)
        {
            const VerbDefinition* verb = Env.Bindings.Verbs->Get(out.Binding.Verb);
            if (verb != nullptr && verb->Kind == VerbKind::Lease)
            {
                Report(where, "is a lease; a sequence invokes instant verbs");
                ok = false;
            }
        }
        if (ok)
        {
            for (const VerbCompiledInput& input : out.Binding.Inputs)
            {
                const auto argument = std::ranges::find(desc.Arguments, input.Name, &SequenceArgumentDesc::Key);
                CompiledSequenceValue value;
                if (argument == desc.Arguments.end() || input.Destinations.empty())
                    ok = false;
                else
                    ok = Value(argument->Value, input.Destinations.front().Expected,
                               std::format("{} argument '{}'", where, input.Name), value)
                        && ok;
                out.Inputs.push_back(std::move(value));
            }
        }
        out.Resolved = ok;
        return out;
    }

private:
    void Report(std::string_view where, std::string message)
    {
        Diagnostics.push_back(std::format("{}: {}", where, message));
    }

    bool Port(std::string_view name, std::string_view where, std::uint8_t& out, SequencePortKind& kind)
    {
        const auto found = std::ranges::find(Env.Ports, name, &SequencePort::Name);
        if (found == Env.Ports.end())
        {
            Report(where, std::format("no port '{}'", name));
            return false;
        }
        out = static_cast<std::uint8_t>(found - Env.Ports.begin());
        kind = found->Kind;
        return true;
    }

    bool EntityPort(std::string_view name, std::string_view where, std::uint8_t& out)
    {
        SequencePortKind kind = SequencePortKind::Entity;
        if (!Port(name, where, out, kind))
            return false;
        if (kind != SequencePortKind::Entity)
        {
            Report(where, std::format("port '{}' is not an entity", name));
            return false;
        }
        return true;
    }

    const AuthoredQueryDefinition* FindQuery(const std::string& name, std::string_view where,
                                             AuthoredQueryHandle& handle)
    {
        handle = Env.Queries != nullptr ? Env.Queries->Resolve(name) : AuthoredQueryHandle{};
        const AuthoredQueryDefinition* query = handle.Slot.IsValid() ? Env.Queries->Get(handle.Slot) : nullptr;
        if (query == nullptr)
            Report(where, std::format("no query '{}'", name));
        return query;
    }

    bool QueryArguments(const AuthoredQueryDefinition& query, const std::vector<SequenceArgumentDesc>& given,
                        std::string_view where, std::vector<CompiledSequenceValue>& out)
    {
        std::vector<std::string_view> names;
        for (const SequenceArgumentDesc& argument : given)
            names.push_back(argument.Key);
        std::vector<std::string_view> undeclared;
        const std::vector<AuthoredArgumentSlot> slots = MatchAuthoredArguments(query.Arguments, names, undeclared);
        for (const std::string_view name : undeclared)
            Report(where, std::format("'{}' takes no argument '{}'", query.Name, name));
        bool ok = undeclared.empty();
        for (const AuthoredArgumentSlot& slot : slots)
        {
            const DataFieldSchema& field = *slot.Field;
            CompiledSequenceValue value;
            if (slot.Given)
            {
                ok = Value(given[*slot.Given].Value, field, std::format("{} argument '{}'", where, field.Key), value)
                    && ok;
            }
            else
            {
                std::vector<std::string> errors;
                if (!CompileAuthoredDefault(field, where, field.Key, value.Constant, errors))
                    ok = false;
                for (std::string& error : errors)
                    Report(where, std::move(error));
            }
            out.push_back(std::move(value));
        }
        return ok;
    }

    // What a value produces must be what `expected` takes; modifiers are
    // checked against the kind they act on.
    bool Value(const SequenceValueDesc& desc, const DataFieldSchema& expected, const std::string& where,
               CompiledSequenceValue& out)
    {
        out.Source = desc.Source;
        const DataFieldSchema& field = Unwrapped(expected);
        if (!desc.Frame.empty())
        {
            if (!IsVector3(field))
            {
                Report(where, "only a direction can be given in an entity's frame");
                return false;
            }
            if (!EntityPort(desc.Frame, where, out.Frame))
                return false;
        }
        if (desc.Scale)
        {
            if (field.Kind != DataFieldKind::Float && field.Kind != DataFieldKind::Vector)
            {
                Report(where, "only a number or a vector can be scaled");
                return false;
            }
            out.Scale = static_cast<float>(*desc.Scale);
        }

        switch (desc.Source)
        {
        case SequenceValueSource::Constant:
        {
            std::vector<std::string> errors;
            const bool ok = CompileAuthoredConstant(desc.Constant, expected, Env.Bindings, where, out.Constant, errors);
            for (std::string& error : errors)
                Report(where, std::move(error));
            return ok;
        }
        case SequenceValueSource::Port:
        {
            SequencePortKind kind = SequencePortKind::Entity;
            if (!Port(desc.Name, where, out.Port, kind))
                return false;
            const bool fits = kind == SequencePortKind::Entity ? field.Kind == DataFieldKind::Entity : IsVector3(field);
            if (!fits)
                Report(where, std::format("port '{}' is not the kind this argument takes", desc.Name));
            return fits;
        }
        case SequenceValueSource::Query:
        {
            const AuthoredQueryDefinition* query = FindQuery(desc.Name, where, out.Query);
            if (query == nullptr)
                return false;
            bool ok = QueryArguments(*query, desc.Arguments, where, out.Arguments);
            const DataFieldSchema* produced = &Unwrapped(query->Result);
            if (!desc.Member.empty())
            {
                const auto member = std::ranges::find(produced->Children, desc.Member, &DataFieldSchema::Key);
                if (produced->Kind != DataFieldKind::Record || member == produced->Children.end())
                {
                    Report(where, std::format("'{}' answers no member '{}'", desc.Name, desc.Member));
                    return false;
                }
                out.Member = static_cast<std::int32_t>(member - produced->Children.begin());
                produced = &*member;
            }
            if (!SameShape(*produced, expected))
            {
                Report(where, std::format("'{}' answers a different kind than this argument takes", desc.Name));
                return false;
            }
            return ok;
        }
        }
        return false;
    }

    const VerbSequenceEnvironment& Env;
    std::vector<std::string>& Diagnostics;
};
} // namespace

void CompileVerbSequence(const VerbSequenceDesc& desc,
                         const VerbSequenceEnvironment& environment,
                         CompiledVerbSequence& out,
                         std::vector<std::string>& diagnostics)
{
    out = {};
    SequenceCompilation compilation(environment, diagnostics);
    for (std::size_t i = 0; i < desc.Conditions.size(); ++i)
        out.Conditions.push_back(compilation.Condition(desc.Conditions[i], i));
    for (std::size_t i = 0; i < desc.Commands.size(); ++i)
        out.Commands.push_back(compilation.Command(desc.Commands[i], i));
}
