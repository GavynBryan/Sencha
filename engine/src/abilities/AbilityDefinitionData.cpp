#include <abilities/AbilityDefinitionData.h>

#include <abilities/AbilityBindingNames.h>
#include <abilities/AbilityCapacity.h>
#include <abilities/AbilityValueNames.h>
#include <authored/VerbBindingData.h>
#include <core/assets/AssetPath.h>
#include <core/metadata/DataSchema.h>

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
    template<typename E>
    using Named = AbilityNamed<E>;

    constexpr std::array<Named<AbilityCompare>, 6> kCompares{ {
        { "at_least", AbilityCompare::AtLeast },
        { "at_most", AbilityCompare::AtMost },
        { "above", AbilityCompare::Above },
        { "below", AbilityCompare::Below },
        { "equals", AbilityCompare::Equals },
        { "not_equals", AbilityCompare::NotEquals },
    } };

    constexpr std::array<Named<AbilityTimeDomain>, 2> kDomains{ {
        { "actor", AbilityTimeDomain::Actor },
        { "simulation", AbilityTimeDomain::Simulation },
    } };

    constexpr std::array<Named<AbilityTransitionTrigger>, 3> kTriggers{ {
        { "release", AbilityTransitionTrigger::Release },
        { "timeout", AbilityTransitionTrigger::Timeout },
        { "complete", AbilityTransitionTrigger::Complete },
    } };

    constexpr std::array<Named<AbilityCostTiming>, 2> kCostTimings{ {
        { "start", AbilityCostTiming::Start },
        { "commit", AbilityCostTiming::Commit },
    } };

    constexpr std::array<Named<AbilityCostRepeat>, 2> kCostRepeats{ {
        { "once", AbilityCostRepeat::Once },
        { "per_pulse", AbilityCostRepeat::PerPulse },
    } };

    constexpr std::array<Named<AbilityCooldownStart>, 3> kCooldownStarts{ {
        { "on_start", AbilityCooldownStart::OnStart },
        { "on_commit", AbilityCooldownStart::OnCommit },
        { "on_end", AbilityCooldownStart::OnEnd },
    } };

    constexpr std::array<Named<AbilityPredictionLevel>, 3> kPredictions{ {
        { "none", AbilityPredictionLevel::None },
        { "presentation", AbilityPredictionLevel::Presentation },
        { "simulation", AbilityPredictionLevel::Simulation },
    } };

    constexpr std::array<Named<AbilityOpKind>, 5> kOpKinds{ {
        { "invoke", AbilityOpKind::Invoke },
        { "query", AbilityOpKind::Query },
        { "set", AbilityOpKind::Set },
        { "commit", AbilityOpKind::Commit },
        { "cue", AbilityOpKind::Cue },
    } };

    constexpr std::array<Named<AbilityConditionKind>, 13> kConditionKinds{ {
        { "tags", AbilityConditionKind::Tags },
        { "attribute", AbilityConditionKind::Attribute },
        { "local", AbilityConditionKind::Local },
        { "hit", AbilityConditionKind::Hit },
        { "entity", AbilityConditionKind::Entity },
        { "phase_ticks", AbilityConditionKind::PhaseTicks },
        { "pulse", AbilityConditionKind::Pulse },
        { "input", AbilityConditionKind::Input },
        { "query", AbilityConditionKind::Query },
        { "all", AbilityConditionKind::All },
        { "any", AbilityConditionKind::Any },
        { "not", AbilityConditionKind::Not },
        { "target_lost", AbilityConditionKind::TargetLost },
    } };

    std::size_t SlotsOf(AbilityLocalType type)
    {
        return type == AbilityLocalType::Hit || type == AbilityLocalType::Entities ? 2 : 1;
    }

    // One pass over one document. Every reader reports where it was and keeps going,
    // so an author sees every problem at once.
    class Reader
    {
    public:
        Reader(std::vector<AbilityDiagnostic>& diagnostics, std::vector<AssetRef>& dependencies)
            : Out(diagnostics)
            , Dependencies(dependencies)
        {
        }

        void Report(AbilityDiagnosticSeverity severity, std::string code, std::string_view key, std::string path,
                    std::string message)
        {
            Out.push_back({ severity, std::move(code), std::string(key), std::move(path), std::move(message) });
        }

        void Error(std::string code, std::string_view key, std::string path, std::string message)
        {
            Report(AbilityDiagnosticSeverity::Error, std::move(code), key, std::move(path), std::move(message));
        }

        void Warn(std::string code, std::string_view key, std::string path, std::string message)
        {
            Report(AbilityDiagnosticSeverity::Warning, std::move(code), key, std::move(path), std::move(message));
        }

        // Fields an element does not know are mistakes worth naming: a misspelled
        // list would otherwise be silently empty.
        void CheckFields(const JsonValue& object, std::initializer_list<std::string_view> known, std::string_view key,
                         const std::string& path)
        {
            if (!object.IsObject())
                return;
            for (const auto& [name, value] : object.AsObject())
                if (std::ranges::find(known, name) == known.end())
                    Error("schema", key, path + "." + name, std::format("'{}' is not a field here", name));
        }

        std::string Text(const JsonValue& object, std::string_view field, std::string_view key,
                         const std::string& path, bool required)
        {
            const JsonValue* value = object.Find(field);
            if (value == nullptr)
            {
                if (required)
                    Error("schema", key, path, std::format("'{}' is required", field));
                return {};
            }
            if (!value->IsString())
            {
                Error("schema", key, path + "." + std::string(field), std::format("'{}' is text", field));
                return {};
            }
            return value->AsString();
        }

        std::string Key(const JsonValue& object, const std::string& path)
        {
            std::string key = Text(object, "key", {}, path, true);
            if (!key.empty() && !IsValidStableKey(key))
                Error("schema", key, path + ".key", std::format("'{}' is not a stable key", key));
            return key;
        }

        std::uint32_t Count(const JsonValue& object, std::string_view field, std::string_view key,
                            const std::string& path, std::uint32_t fallback, std::uint32_t minimum)
        {
            const JsonValue* value = object.Find(field);
            if (value == nullptr)
                return fallback;
            const double number = value->IsNumber() ? value->AsNumber() : -1.0;
            if (!value->IsNumber() || number < minimum || number > 4.0e9
                || number != static_cast<double>(static_cast<std::uint32_t>(number)))
            {
                Error("schema", key, path + "." + std::string(field),
                      std::format("'{}' is a whole number of at least {}", field, minimum));
                return fallback;
            }
            return static_cast<std::uint32_t>(number);
        }

        template<typename E, std::size_t N>
        E Choice(const JsonValue& object, std::string_view field, const std::array<Named<E>, N>& choices,
                 std::string_view key, const std::string& path, E fallback)
        {
            const JsonValue* value = object.Find(field);
            if (value == nullptr)
                return fallback;
            if (value->IsString())
                for (const Named<E>& choice : choices)
                    if (choice.Name == value->AsString())
                        return choice.Value;
            Error("schema", key, path + "." + std::string(field), std::format("'{}' is not a choice of '{}'",
                                                                              value->IsString() ? value->AsString() : "",
                                                                              field));
            return fallback;
        }

        template<typename E, std::size_t N>
        std::optional<E> OptionalChoice(const JsonValue& object, std::string_view field,
                                        const std::array<Named<E>, N>& choices, std::string_view key,
                                        const std::string& path)
        {
            if (object.Find(field) == nullptr)
                return std::nullopt;
            return Choice(object, field, choices, key, path, choices.front().Value);
        }

        std::vector<std::string> Names(const JsonValue& object, std::string_view field, std::string_view key,
                                       const std::string& path)
        {
            std::vector<std::string> names;
            const JsonValue* list = object.Find(field);
            if (list == nullptr)
                return names;
            if (!list->IsArray())
            {
                Error("schema", key, path + "." + std::string(field), std::format("'{}' is a list of names", field));
                return names;
            }
            for (std::size_t i = 0; i < list->AsArray().size(); ++i)
            {
                const JsonValue& name = list->AsArray()[i];
                if (!name.IsString() || name.AsString().empty())
                    Error("schema", key, std::format("{}.{}[{}]", path, field, i), "a name");
                else
                    names.push_back(name.AsString());
            }
            return names;
        }

        AbilityTagQueryDecl TagQuery(const JsonValue& value, std::string_view key, const std::string& path)
        {
            AbilityTagQueryDecl query;
            if (!value.IsObject())
            {
                Error("schema", key, path, "a tag query is { all, any, none }");
                return query;
            }
            CheckFields(value, { "all", "any", "none" }, key, path);
            query.All = Names(value, "all", key, path);
            query.Any = Names(value, "any", key, path);
            query.None = Names(value, "none", key, path);
            return query;
        }

        bool Value(const JsonValue& value, std::string_view key, const std::string& path, AbilityValueDecl& out)
        {
            if (value.IsNumber() || value.IsBool())
            {
                out.Source = AbilityValueSource::Constant;
                out.Constant.Source = VerbArgumentSource::Literal;
                out.Constant.Literal = value;
                return true;
            }
            if (!value.IsObject())
            {
                Error("schema", key, path, "a value is a number or one of {const}, {tag}, {data}, {asset}, "
                                           "{entity}, {port}, {local}");
                return false;
            }
            if (const JsonValue* port = value.Find("port"))
            {
                CheckFields(value, { "port" }, key, path);
                out.Source = AbilityValueSource::Port;
                const auto found = std::ranges::find_if(kAbilityPortNames, [&](const auto& p) {
                    return port->IsString() && p.Name == port->AsString();
                });
                if (found == kAbilityPortNames.end())
                {
                    Error("schema", key, path + ".port", "not a port");
                    return false;
                }
                out.Port = found->Value;
                return true;
            }
            if (const JsonValue* local = value.Find("local"))
            {
                CheckFields(value, { "local", "field" }, key, path);
                out.Source = AbilityValueSource::Local;
                if (!local->IsString() || local->AsString().empty())
                {
                    Error("schema", key, path + ".local", "a local's key");
                    return false;
                }
                out.Local = local->AsString();
                out.Field = Choice(value, "field", kAbilityHitFieldNames, key, path, AbilityHitField::None);
                return true;
            }
            std::string error;
            out.Source = AbilityValueSource::Constant;
            if (!ParseVerbBindingArgument(key, "value", value, out.Constant, Dependencies, error))
            {
                Error("schema", key, path, error);
                return false;
            }
            if (out.Constant.Source == VerbArgumentSource::Input)
            {
                Error("schema", key, path, "an ability's values come from a port or a local, not an input");
                return false;
            }
            return true;
        }

        std::vector<AbilityArgumentDecl> Arguments(const JsonValue& object, std::string_view key,
                                                   const std::string& path)
        {
            std::vector<AbilityArgumentDecl> arguments;
            const JsonValue* list = object.Find("arguments");
            if (list == nullptr)
                return arguments;
            if (!list->IsObject())
            {
                Error("schema", key, path + ".arguments", "arguments are { name: value }");
                return arguments;
            }
            for (const auto& [name, value] : list->AsObject())
            {
                AbilityArgumentDecl argument;
                argument.Name = name;
                if (Value(value, key, path + ".arguments." + name, argument.Value))
                    arguments.push_back(std::move(argument));
            }
            return arguments;
        }

        // A comparison: exactly one of the comparison keys, holding a number or a value.
        void Comparison(const JsonValue& object, AbilityConditionDecl& out, const std::string& path)
        {
            const Named<AbilityCompare>* chosen = nullptr;
            for (const Named<AbilityCompare>& compare : kCompares)
            {
                if (object.Find(compare.Name) == nullptr)
                    continue;
                if (chosen != nullptr)
                    Error("schema", out.Key, path, "one comparison per condition");
                chosen = &compare;
            }
            if (chosen == nullptr)
            {
                Error("schema", out.Key, path, "a comparison: at_least, at_most, above, below, equals or not_equals");
                return;
            }
            out.Compare = chosen->Value;
            (void)Value(*object.Find(chosen->Name), out.Key, path + "." + std::string(chosen->Name), out.Value);
        }

        bool Condition(const JsonValue& value, const std::string& path, std::size_t depth, AbilityConditionDecl& out)
        {
            if (!value.IsObject())
            {
                Error("schema", {}, path, "a condition is an object with a key and one kind");
                return false;
            }
            out.Path = path;
            out.Key = Key(value, path);
            const Named<AbilityConditionKind>* kind = nullptr;
            for (const auto& candidate : kConditionKinds)
            {
                if (value.Find(candidate.Name) == nullptr)
                    continue;
                if (kind != nullptr)
                    Error("schema", out.Key, path, "a condition has exactly one kind");
                kind = &candidate;
            }
            if (kind == nullptr)
            {
                Error("schema", out.Key, path, "a condition names its kind: tags, attribute, local, hit, entity, "
                                               "phase_ticks, pulse, input, query, all, any or not");
                return false;
            }
            CheckFields(value, { "key", kind->Name }, out.Key, path);
            out.Kind = kind->Value;
            const JsonValue& body = *value.Find(kind->Name);
            const std::string bodyPath = path + "." + std::string(kind->Name);
            switch (out.Kind)
            {
            case AbilityConditionKind::Tags:
                out.Tags = TagQuery(body, out.Key, bodyPath);
                break;
            case AbilityConditionKind::Attribute:
                CheckFields(body, { "name", "at_least", "at_most", "above", "below", "equals", "not_equals" }, out.Key,
                            bodyPath);
                out.Name = Text(body, "name", out.Key, bodyPath, true);
                Comparison(body, out, bodyPath);
                break;
            case AbilityConditionKind::Local:
                CheckFields(body, { "of", "at_least", "at_most", "above", "below", "equals", "not_equals" }, out.Key,
                            bodyPath);
                out.Subject.Source = AbilityValueSource::Local;
                out.Subject.Local = Text(body, "of", out.Key, bodyPath, true);
                Comparison(body, out, bodyPath);
                break;
            case AbilityConditionKind::Hit:
            case AbilityConditionKind::TargetLost:
                if (!body.IsString() || body.AsString().empty())
                    Error("schema", out.Key, bodyPath,
                          out.Kind == AbilityConditionKind::Hit ? "a hit condition names a hit local"
                                                                : "a lost target names the local holding it");
                else
                {
                    out.Subject.Source = AbilityValueSource::Local;
                    out.Subject.Local = body.AsString();
                }
                break;
            case AbilityConditionKind::Entity:
                CheckFields(body, { "of", "has", "tags" }, out.Key, bodyPath);
                if (const JsonValue* of = body.Find("of"))
                    (void)Value(*of, out.Key, bodyPath + ".of", out.Subject);
                else
                    Error("schema", out.Key, bodyPath, "an entity condition names the entity it checks ('of')");
                out.Components = Names(body, "has", out.Key, bodyPath);
                if (const JsonValue* tags = body.Find("tags"))
                    out.Tags = TagQuery(*tags, out.Key, bodyPath + ".tags");
                break;
            case AbilityConditionKind::PhaseTicks:
            case AbilityConditionKind::Pulse:
                CheckFields(body, { "at_least", "at_most", "above", "below", "equals", "not_equals" }, out.Key, bodyPath);
                Comparison(body, out, bodyPath);
                break;
            case AbilityConditionKind::Input:
                if (!body.IsString() || (body.AsString() != "held" && body.AsString() != "not_held"))
                    Error("schema", out.Key, bodyPath, "an input condition is 'held' or 'not_held'");
                else
                    out.Held = body.AsString() == "held";
                break;
            case AbilityConditionKind::Query:
                CheckFields(body, { "name", "arguments" }, out.Key, bodyPath);
                out.Name = Text(body, "name", out.Key, bodyPath, true);
                out.Arguments = Arguments(body, out.Key, bodyPath);
                break;
            case AbilityConditionKind::All:
            case AbilityConditionKind::Any:
            case AbilityConditionKind::Not:
            {
                if (depth + 1 >= kAbilityConditionDepth)
                {
                    Error("capacity", out.Key, bodyPath,
                          std::format("conditions nest at most {} deep", kAbilityConditionDepth));
                    break;
                }
                if (out.Kind == AbilityConditionKind::Not)
                {
                    AbilityConditionDecl child;
                    if (Condition(body, bodyPath, depth + 1, child))
                        out.Children.push_back(std::move(child));
                    break;
                }
                if (!body.IsArray() || body.AsArray().empty())
                {
                    Error("schema", out.Key, bodyPath, "a list of conditions");
                    break;
                }
                for (std::size_t i = 0; i < body.AsArray().size(); ++i)
                {
                    AbilityConditionDecl child;
                    if (Condition(body.AsArray()[i], std::format("{}[{}]", bodyPath, i), depth + 1, child))
                        out.Children.push_back(std::move(child));
                }
                break;
            }
            }
            return true;
        }

        std::vector<AbilityConditionDecl> Conditions(const JsonValue& object, std::string_view field,
                                                     const std::string& path)
        {
            std::vector<AbilityConditionDecl> conditions;
            const JsonValue* list = object.Find(field);
            if (list == nullptr)
                return conditions;
            const std::string listPath = path + "." + std::string(field);
            if (!list->IsArray())
            {
                Error("schema", {}, listPath, "a list of conditions");
                return conditions;
            }
            for (std::size_t i = 0; i < list->AsArray().size(); ++i)
            {
                AbilityConditionDecl condition;
                if (Condition(list->AsArray()[i], std::format("{}[{}]", listPath, i), 0, condition))
                    conditions.push_back(std::move(condition));
            }
            return conditions;
        }

        std::optional<AbilityConditionDecl> When(const JsonValue& object, const std::string& path)
        {
            const JsonValue* when = object.Find("when");
            if (when == nullptr)
                return std::nullopt;
            AbilityConditionDecl condition;
            if (!Condition(*when, path + ".when", 0, condition))
                return std::nullopt;
            return condition;
        }

        bool Op(const JsonValue& value, const std::string& path, AbilityOpDecl& out)
        {
            if (!value.IsObject())
            {
                Error("schema", {}, path, "an op is an object");
                return false;
            }
            out.Path = path;
            out.Key = Key(value, path);
            out.When = When(value, path);
            if (value.Find("lease") != nullptr)
            {
                CheckFields(value, { "key", "lease", "when", "arguments", "targets" }, out.Key, path);
                out.Kind = AbilityOpKind::Lease;
                out.Operation = Text(value, "lease", out.Key, path, true);
                out.Arguments = Arguments(value, out.Key, path);
                out.Targets = Text(value, "targets", out.Key, path, false);
                return true;
            }
            out.Kind = Choice(value, "op", kOpKinds, out.Key, path, AbilityOpKind::Invoke);
            if (value.Find("op") == nullptr)
                Error("schema", out.Key, path, "an op names its kind ('op') or the lease it holds ('lease')");
            switch (out.Kind)
            {
            case AbilityOpKind::Invoke:
                CheckFields(value, { "key", "op", "when", "verb", "arguments", "targets" }, out.Key, path);
                out.Operation = Text(value, "verb", out.Key, path, true);
                out.Arguments = Arguments(value, out.Key, path);
                out.Targets = Text(value, "targets", out.Key, path, false);
                break;
            case AbilityOpKind::Query:
                CheckFields(value, { "key", "op", "when", "query", "arguments", "into" }, out.Key, path);
                out.Operation = Text(value, "query", out.Key, path, true);
                out.Arguments = Arguments(value, out.Key, path);
                out.Local = Text(value, "into", out.Key, path, true);
                break;
            case AbilityOpKind::Set:
                CheckFields(value, { "key", "op", "when", "local", "from" }, out.Key, path);
                out.Local = Text(value, "local", out.Key, path, true);
                if (const JsonValue* from = value.Find("from"))
                    (void)Value(*from, out.Key, path + ".from", out.From);
                else
                    Error("schema", out.Key, path, "'from' is required");
                break;
            case AbilityOpKind::Commit:
            {
                CheckFields(value, { "key", "op", "when", "groups", "on_fail" }, out.Key, path);
                out.Groups = Names(value, "groups", out.Key, path);
                if (out.Groups.empty())
                    Error("schema", out.Key, path, "a commit names the cost groups it pays");
                const std::string onFail = Text(value, "on_fail", out.Key, path, false);
                if (!onFail.empty() && onFail != "end" && onFail != "continue")
                    Error("schema", out.Key, path + ".on_fail", "'end' or 'continue'");
                out.ContinueOnFail = onFail == "continue";
                break;
            }
            case AbilityOpKind::Cue:
                CheckFields(value, { "key", "op", "when", "cue", "at" }, out.Key, path);
                out.Cue = Text(value, "cue", out.Key, path, true);
                if (const JsonValue* at = value.Find("at"))
                {
                    AbilityValueDecl where;
                    if (Value(*at, out.Key, path + ".at", where))
                        out.At = std::move(where);
                }
                break;
            case AbilityOpKind::Lease:
                break;
            }
            return true;
        }

        std::vector<AbilityOpDecl> Ops(const JsonValue& object, std::string_view field, const std::string& path)
        {
            std::vector<AbilityOpDecl> ops;
            const JsonValue* list = object.Find(field);
            if (list == nullptr)
                return ops;
            const std::string listPath = path + "." + std::string(field);
            if (!list->IsArray())
            {
                Error("schema", {}, listPath, "a list of ops");
                return ops;
            }
            for (std::size_t i = 0; i < list->AsArray().size(); ++i)
            {
                AbilityOpDecl op;
                if (Op(list->AsArray()[i], std::format("{}[{}]", listPath, i), op))
                    ops.push_back(std::move(op));
            }
            return ops;
        }

        AbilityPhaseDecl Phase(const JsonValue& value, const std::string& path)
        {
            AbilityPhaseDecl phase;
            phase.Path = path;
            if (!value.IsObject())
            {
                Error("schema", {}, path, "a phase is an object");
                return phase;
            }
            phase.Key = Key(value, path);
            CheckFields(value, { "key", "name", "timing", "enter", "while", "hold", "exit", "present", "continue", "then" },
                        phase.Key, path);
            phase.Name = Text(value, "name", phase.Key, path, false);
            if (const JsonValue* timing = value.Find("timing"))
            {
                const std::string timingPath = path + ".timing";
                CheckFields(*timing, { "domain", "progress_ticks" }, phase.Key, timingPath);
                phase.Domain = Choice(*timing, "domain", kDomains, phase.Key, timingPath, AbilityTimeDomain::Actor);
                phase.ProgressTicks = Count(*timing, "progress_ticks", phase.Key, timingPath, 0, 1);
            }
            phase.Enter = Ops(value, "enter", path);
            if (const JsonValue* pulse = value.Find("while"))
            {
                const std::string pulsePath = path + ".while";
                CheckFields(*pulse, { "every", "first", "ops" }, phase.Key, pulsePath);
                AbilityPulseDecl declared;
                declared.Every = Count(*pulse, "every", phase.Key, pulsePath, 1, 1);
                const std::string first = Text(*pulse, "first", phase.Key, pulsePath, false);
                if (!first.empty() && first != "on_entry" && first != "after_interval")
                    Error("schema", phase.Key, pulsePath + ".first", "'on_entry' or 'after_interval'");
                declared.FirstOnEntry = first != "after_interval";
                declared.Ops = Ops(*pulse, "ops", pulsePath);
                phase.While = std::move(declared);
            }
            phase.Hold = Ops(value, "hold", path);
            phase.Exit = Ops(value, "exit", path);
            if (const JsonValue* present = value.Find("present"); present != nullptr && present->IsArray())
            {
                for (std::size_t i = 0; i < present->AsArray().size(); ++i)
                {
                    const JsonValue& entry = present->AsArray()[i];
                    const std::string entryPath = std::format("{}.present[{}]", path, i);
                    AbilityPresentDecl declared;
                    declared.Key = Key(entry, entryPath);
                    CheckFields(entry, { "key", "cue" }, declared.Key, entryPath);
                    declared.Cue = Text(entry, "cue", declared.Key, entryPath, true);
                    phase.Present.push_back(std::move(declared));
                }
            }
            phase.Continue = Conditions(value, "continue", path);
            if (const JsonValue* then = value.Find("then"); then != nullptr && then->IsArray())
            {
                for (std::size_t i = 0; i < then->AsArray().size(); ++i)
                {
                    const JsonValue& entry = then->AsArray()[i];
                    const std::string entryPath = std::format("{}.then[{}]", path, i);
                    AbilityTransitionDecl transition;
                    transition.Path = entryPath;
                    transition.Key = Key(entry, entryPath);
                    CheckFields(entry, { "key", "on", "ticks", "when", "to" }, transition.Key, entryPath);
                    transition.Trigger = Choice(entry, "on", kTriggers, transition.Key, entryPath,
                                                AbilityTransitionTrigger::Immediate);
                    transition.Ticks = Count(entry, "ticks", transition.Key, entryPath, 0, 1);
                    if (transition.Trigger == AbilityTransitionTrigger::Timeout && transition.Ticks == 0)
                        Error("schema", transition.Key, entryPath, "a timeout waits at least one tick ('ticks')");
                    transition.When = When(entry, entryPath);
                    transition.To = Text(entry, "to", transition.Key, entryPath, true);
                    if (transition.To == "end")
                        transition.To.clear();
                    phase.Then.push_back(std::move(transition));
                }
            }
            return phase;
        }

        AbilityBindingDecl Binding(const JsonValue& value, const std::string& path)
        {
            AbilityBindingDecl binding;
            CheckFields(value, { "action", "policy", "priority", "consume", "when_blocked", "context", "on_interrupt" }, {},
                        path);
            if (const JsonValue* action = value.Find("action"))
            {
                if (action->IsString() && !action->AsString().empty())
                    binding.Action = action->AsString();
                else
                    Error("schema", {}, path + ".action", "an input action's name");
            }
            binding.Policy = OptionalChoice(value, "policy", kAbilityInputPolicyNames, {}, path);
            if (const JsonValue* priority = value.Find("priority"))
            {
                if (priority->IsNumber() && priority->AsNumber() == static_cast<double>(static_cast<std::int32_t>(priority->AsNumber())))
                    binding.Priority = static_cast<std::int32_t>(priority->AsNumber());
                else
                    Error("schema", {}, path + ".priority", "a whole number");
            }
            binding.Consume = OptionalChoice(value, "consume", kAbilityConsumeNames, {}, path);
            binding.WhenBlocked = OptionalChoice(value, "when_blocked", kAbilityWhenBlockedNames, {}, path);
            if (const JsonValue* context = value.Find("context"))
                binding.Context = TagQuery(*context, {}, path + ".context");
            binding.OnInterrupt = OptionalChoice(value, "on_interrupt", kAbilityInterruptNames, {}, path);
            return binding;
        }

        std::string DataReference(const JsonValue& value, std::string_view key, const std::string& path)
        {
            const JsonValue* data = value.IsObject() ? value.Find("data") : nullptr;
            if (data == nullptr || !data->IsString() || !data->AsString().starts_with(kAssetPathPrefix))
            {
                Error("schema", key, path, "a reference is { \"data\": \"asset://...\" }");
                return {};
            }
            Dependencies.push_back(AssetRef{ .Type = AssetType::Data, .Path = data->AsString() });
            return data->AsString();
        }

        void GrantEntries(const JsonValue& list, const std::string& path, std::vector<AbilityGrantDeclaration>& out)
        {
            if (!list.IsArray())
            {
                Error("schema", {}, path, "a list of grant entries");
                return;
            }
            std::unordered_set<std::string> keys;
            for (std::size_t i = 0; i < list.AsArray().size(); ++i)
            {
                const JsonValue& entry = list.AsArray()[i];
                const std::string entryPath = std::format("{}[{}]", path, i);
                AbilityGrantDeclaration grant;
                grant.Key = Key(entry, entryPath);
                CheckFields(entry, { "key", "ability", "binding" }, grant.Key, entryPath);
                if (!grant.Key.empty() && !keys.insert(grant.Key).second)
                    Error("key_duplicate", grant.Key, entryPath, std::format("two grants share the key '{}'", grant.Key));
                if (const JsonValue* ability = entry.Find("ability"))
                    grant.Ability = DataReference(*ability, grant.Key, entryPath + ".ability");
                else
                    Error("schema", grant.Key, entryPath, "'ability' is required");
                if (const JsonValue* binding = entry.Find("binding"))
                    grant.Override = Binding(*binding, entryPath + ".binding");
                out.push_back(std::move(grant));
            }
        }

        void Root(const JsonValue& data, AbilityDeclaration& out)
        {
            const std::string root = "$.data";
            CheckFields(data,
                        { "display", "input_default", "activation", "continuation", "cancel_on", "owned_tags",
                          "cancel_tags", "instances", "locals", "costs", "cooldown", "phases", "prediction", "lifetime" },
                        {}, root);
            if (const JsonValue* display = data.Find("display"))
            {
                CheckFields(*display, { "name", "category" }, {}, root + ".display");
                out.DisplayName = Text(*display, "name", {}, root + ".display", false);
                out.Category = Text(*display, "category", {}, root + ".display", false);
            }
            if (const JsonValue* input = data.Find("input_default"))
                out.InputDefault = Binding(*input, root + ".input_default");
            out.Activation = Conditions(data, "activation", root);
            out.Continuation = Conditions(data, "continuation", root);
            out.CancelOn = Conditions(data, "cancel_on", root);
            out.OwnedTags = Names(data, "owned_tags", {}, root);
            if (out.OwnedTags.size() > kAbilityOwnedTagCapacity)
                Error("capacity", {}, root + ".owned_tags",
                      std::format("an ability owns at most {} tags", kAbilityOwnedTagCapacity));
            out.CancelTags = Names(data, "cancel_tags", {}, root);
            const std::uint32_t instances = Count(data, "instances", {}, root, 1, 1);
            if (instances > kAbilityLiveRecordCapacity)
                Error("capacity", {}, root + ".instances",
                      std::format("at most {} instances run at once", kAbilityLiveRecordCapacity));
            out.Instances = static_cast<std::uint8_t>(std::min<std::uint32_t>(instances, kAbilityLiveRecordCapacity));

            if (const JsonValue* locals = data.Find("locals"); locals != nullptr && locals->IsArray())
            {
                for (std::size_t i = 0; i < locals->AsArray().size(); ++i)
                {
                    const JsonValue& entry = locals->AsArray()[i];
                    const std::string path = std::format("{}.locals[{}]", root, i);
                    AbilityLocalDecl local;
                    local.Path = path;
                    local.Key = Key(entry, path);
                    CheckFields(entry, { "key", "name", "type" }, local.Key, path);
                    local.Name = Text(entry, "name", local.Key, path, false);
                    local.Type = Choice(entry, "type", kAbilityLocalTypeNames, local.Key, path, AbilityLocalType::Float);
                    if (entry.Find("type") == nullptr)
                        Error("schema", local.Key, path, "'type' is required");
                    out.Locals.push_back(std::move(local));
                }
            }

            if (const JsonValue* costs = data.Find("costs"); costs != nullptr && costs->IsArray())
            {
                for (std::size_t i = 0; i < costs->AsArray().size(); ++i)
                {
                    const JsonValue& entry = costs->AsArray()[i];
                    const std::string path = std::format("{}.costs[{}]", root, i);
                    AbilityCostGroupDecl group;
                    group.Path = path;
                    group.Key = Key(entry, path);
                    CheckFields(entry, { "key", "name", "when", "repeat", "effects" }, group.Key, path);
                    group.Name = Text(entry, "name", group.Key, path, false);
                    group.Timing = Choice(entry, "when", kCostTimings, group.Key, path, AbilityCostTiming::Commit);
                    group.Repeat = Choice(entry, "repeat", kCostRepeats, group.Key, path, AbilityCostRepeat::Once);
                    if (const JsonValue* effects = entry.Find("effects"); effects != nullptr && effects->IsArray())
                        for (std::size_t e = 0; e < effects->AsArray().size(); ++e)
                            if (std::string effect = DataReference(effects->AsArray()[e], group.Key,
                                                                   std::format("{}.effects[{}]", path, e));
                                !effect.empty())
                                group.Effects.push_back(std::move(effect));
                    if (group.Effects.empty())
                        Error("schema", group.Key, path, "a cost group names the effects it pays");
                    out.Costs.push_back(std::move(group));
                }
            }

            if (const JsonValue* cooldown = data.Find("cooldown"))
            {
                const std::string path = root + ".cooldown";
                CheckFields(*cooldown, { "effect", "start", "group" }, {}, path);
                AbilityCooldownDecl declared;
                if (const JsonValue* effect = cooldown->Find("effect"))
                    declared.Effect = DataReference(*effect, {}, path + ".effect");
                else
                    Error("schema", {}, path, "'effect' is required");
                declared.Start = Choice(*cooldown, "start", kCooldownStarts, {}, path, AbilityCooldownStart::OnStart);
                declared.Group = Text(*cooldown, "group", {}, path, declared.Start == AbilityCooldownStart::OnCommit);
                out.Cooldown = std::move(declared);
            }

            if (const JsonValue* phases = data.Find("phases"); phases != nullptr && phases->IsArray())
                for (std::size_t i = 0; i < phases->AsArray().size(); ++i)
                    out.Phases.push_back(Phase(phases->AsArray()[i], std::format("{}.phases[{}]", root, i)));
            if (out.Phases.empty())
                Error("schema", {}, root + ".phases", "an ability has at least one phase");

            out.Prediction = Choice(data, "prediction", kPredictions, {}, root, AbilityPredictionLevel::Presentation);
            if (const JsonValue* lifetime = data.Find("lifetime"))
            {
                CheckFields(*lifetime, { "on_reload" }, {}, root + ".lifetime");
                const std::string onReload = Text(*lifetime, "on_reload", {}, root + ".lifetime", false);
                if (!onReload.empty() && onReload != "cancel" && onReload != "keep")
                    Error("schema", {}, root + ".lifetime.on_reload", "'cancel' or 'keep'");
                out.CancelOnReload = onReload != "keep";
            }
        }

    private:
        std::vector<AbilityDiagnostic>& Out;
        std::vector<AssetRef>& Dependencies;
    };

    // The checks that read the whole declaration: references between elements and
    // the shape of the phase graph.
    class Checker
    {
    public:
        Checker(const AbilityDeclaration& declaration, Reader& reader)
            : Declared(declaration)
            , Report(reader)
        {
        }

        void Run()
        {
            Keys();
            Locals();
            Costs();
            for (const AbilityConditionDecl& condition : Declared.Activation)
                CheckCondition(condition, nullptr);
            for (const AbilityConditionDecl& condition : Declared.Continuation)
                CheckCondition(condition, nullptr);
            for (const AbilityConditionDecl& condition : Declared.CancelOn)
                CheckCondition(condition, nullptr);
            Phases();
            Graph();
        }

    private:
        void Unique(std::unordered_set<std::string>& seen, const std::string& key, const std::string& path,
                    std::string_view kind)
        {
            if (!key.empty() && !seen.insert(key).second)
                Report.Error("key_duplicate", key, path, std::format("two {} share the key '{}'", kind, key));
        }

        void ConditionKeys(const AbilityConditionDecl& condition)
        {
            Unique(ConditionSeen, condition.Key, condition.Path, "conditions");
            for (const AbilityConditionDecl& child : condition.Children)
                ConditionKeys(child);
        }

        void OpKeys(const std::vector<AbilityOpDecl>& ops)
        {
            for (const AbilityOpDecl& op : ops)
            {
                Unique(OpSeen, op.Key, op.Path, "ops");
                if (op.When)
                    ConditionKeys(*op.When);
            }
        }

        void Keys()
        {
            std::unordered_set<std::string> phases, transitions, locals, groups, presents;
            for (const auto* list : { &Declared.Activation, &Declared.Continuation, &Declared.CancelOn })
                for (const AbilityConditionDecl& condition : *list)
                    ConditionKeys(condition);
            for (const AbilityLocalDecl& local : Declared.Locals)
                Unique(locals, local.Key, local.Path, "locals");
            for (const AbilityCostGroupDecl& group : Declared.Costs)
                Unique(groups, group.Key, group.Path, "cost groups");
            for (const AbilityPhaseDecl& phase : Declared.Phases)
            {
                Unique(phases, phase.Key, phase.Path, "phases");
                OpKeys(phase.Enter);
                if (phase.While)
                    OpKeys(phase.While->Ops);
                OpKeys(phase.Hold);
                OpKeys(phase.Exit);
                for (const AbilityPresentDecl& present : phase.Present)
                    Unique(presents, present.Key, phase.Path, "presentations");
                for (const AbilityConditionDecl& condition : phase.Continue)
                    ConditionKeys(condition);
                for (const AbilityTransitionDecl& transition : phase.Then)
                {
                    Unique(transitions, transition.Key, transition.Path, "transitions");
                    if (transition.When)
                        ConditionKeys(*transition.When);
                }
            }
        }

        void Locals()
        {
            std::size_t slots = 0;
            std::size_t lists = 0;
            for (const AbilityLocalDecl& local : Declared.Locals)
            {
                slots += SlotsOf(local.Type);
                lists += local.Type == AbilityLocalType::Entities ? 1 : 0;
                LocalTypes.emplace(local.Key, local.Type);
            }
            if (slots > kAbilityLocalSlotCapacity)
                Report.Error("capacity", {}, "$.data.locals",
                             std::format("{} local slots declared; limit {} (a hit or an entity list takes two)", slots,
                                         kAbilityLocalSlotCapacity));
            if (lists > 1)
                Report.Error("capacity", {}, "$.data.locals", "one entities local per ability");
        }

        void Costs()
        {
            if (Declared.Costs.size() > kAbilityCostGroupCapacity)
                Report.Error("capacity", {}, "$.data.costs",
                             std::format("{} cost groups declared; limit {}", Declared.Costs.size(),
                                         kAbilityCostGroupCapacity));
            for (const AbilityCostGroupDecl& group : Declared.Costs)
                Groups.emplace(group.Key, &group);
            if (Declared.Cooldown && Declared.Cooldown->Start == AbilityCooldownStart::OnCommit)
            {
                const auto found = Groups.find(Declared.Cooldown->Group);
                if (found == Groups.end())
                    Report.Error("group_unknown", Declared.Cooldown->Group, "$.data.cooldown.group",
                                 std::format("no cost group '{}'", Declared.Cooldown->Group));
                else if (found->second->Timing != AbilityCostTiming::Commit)
                    Report.Error("cost_shape", found->first, "$.data.cooldown.group",
                                 "a cooldown that starts on a commit names a group paid by a commit");
            }
        }

        const AbilityLocalType* TypeOf(const std::string& local) const
        {
            const auto found = LocalTypes.find(local);
            return found != LocalTypes.end() ? &found->second : nullptr;
        }

        // Whether the value is an entity that may be stale or absent when read.
        static bool IsEntityFromLocal(const AbilityValueDecl& value, const AbilityLocalType* type)
        {
            if (value.Source == AbilityValueSource::Port)
                return value.Port == AbilityPort::Target;
            if (value.Source != AbilityValueSource::Local || type == nullptr)
                return false;
            return *type == AbilityLocalType::Entity
                || (*type == AbilityLocalType::Hit && value.Field == AbilityHitField::Entity);
        }

        static bool SameSource(const AbilityValueDecl& a, const AbilityValueDecl& b)
        {
            return a.Source == b.Source && a.Port == b.Port && a.Local == b.Local && a.Field == b.Field;
        }

        // A guard counts when it is the op's condition or one of a top-level `all`'s.
        static bool Guards(const std::optional<AbilityConditionDecl>& when,
                           const std::function<bool(const AbilityConditionDecl&)>& matches)
        {
            if (!when)
                return false;
            if (matches(*when))
                return true;
            if (when->Kind != AbilityConditionKind::All)
                return false;
            return std::ranges::any_of(when->Children, matches);
        }

        void CheckValue(const AbilityValueDecl& value, const std::string& key, const std::string& path,
                        const AbilityOpDecl* op, std::string_view role)
        {
            if (value.Source == AbilityValueSource::Port && value.Port == AbilityPort::Target
                && (op == nullptr || op->Targets.empty()))
                Report.Error("port_unknown", key, path, "'target' is only a port of an op with 'targets'");
            if (value.Source != AbilityValueSource::Local)
                return;
            const AbilityLocalType* type = TypeOf(value.Local);
            if (type == nullptr)
            {
                Report.Error("local_unknown", key, path, std::format("no local '{}'", value.Local));
                return;
            }
            if (value.Field != AbilityHitField::None && *type != AbilityLocalType::Hit)
                Report.Error("local_type", key, path, std::format("'{}' is not a hit, so it has no fields", value.Local));
            if (*type == AbilityLocalType::Entities)
                Report.Error("local_type", key, path,
                             std::format("'{}' is an entity list, which an op reaches through 'targets'", value.Local));
            if (op == nullptr)
                return;
            if (*type == AbilityLocalType::Hit
                && (value.Field == AbilityHitField::Point || value.Field == AbilityHitField::Normal
                    || value.Field == AbilityHitField::None))
            {
                const bool guarded = Guards(op->When, [&](const AbilityConditionDecl& condition) {
                    return condition.Kind == AbilityConditionKind::Hit && condition.Subject.Local == value.Local;
                });
                if (!guarded)
                    Report.Error("unguarded_maybe_empty", key, path,
                                 std::format("{} comes from '{}', which may not have struck anything; add "
                                             "'when: hit {}'",
                                             role, value.Local, value.Local));
            }
            if (IsEntityFromLocal(value, type))
            {
                const bool guarded = Guards(op->When, [&](const AbilityConditionDecl& condition) {
                    return condition.Kind == AbilityConditionKind::Entity && SameSource(condition.Subject, value);
                });
                if (!guarded)
                    Report.Error("unguarded_recipient", key, path,
                                 std::format("{} may name an entity that is gone; add an 'entity' guard on it", role));
            }
        }

        void CheckCondition(const AbilityConditionDecl& condition, const AbilityOpDecl* op)
        {
            switch (condition.Kind)
            {
            case AbilityConditionKind::Local:
            case AbilityConditionKind::Hit:
            case AbilityConditionKind::TargetLost:
            {
                const AbilityLocalType* type = TypeOf(condition.Subject.Local);
                if (type == nullptr)
                    Report.Error("local_unknown", condition.Key, condition.Path,
                                 std::format("no local '{}'", condition.Subject.Local));
                else if (condition.Kind == AbilityConditionKind::TargetLost && *type != AbilityLocalType::Entity
                         && *type != AbilityLocalType::Hit)
                    Report.Error("local_type", condition.Key, condition.Path,
                                 std::format("'{}' does not hold one entity to lose", condition.Subject.Local));
                else if (condition.Kind == AbilityConditionKind::Hit && *type != AbilityLocalType::Hit)
                    Report.Error("local_type", condition.Key, condition.Path,
                                 std::format("'{}' is not a hit", condition.Subject.Local));
                else if (condition.Kind == AbilityConditionKind::Local
                         && (*type == AbilityLocalType::Hit || *type == AbilityLocalType::Entities
                             || *type == AbilityLocalType::Vec3 || *type == AbilityLocalType::Entity))
                    Report.Error("local_type", condition.Key, condition.Path,
                                 std::format("'{}' is not a number to compare", condition.Subject.Local));
                break;
            }
            case AbilityConditionKind::Entity:
                if (condition.Subject.Source == AbilityValueSource::Local)
                {
                    const AbilityLocalType* type = TypeOf(condition.Subject.Local);
                    if (type == nullptr)
                        Report.Error("local_unknown", condition.Key, condition.Path,
                                     std::format("no local '{}'", condition.Subject.Local));
                    else if (!IsEntityFromLocal(condition.Subject, type))
                        Report.Error("local_type", condition.Key, condition.Path,
                                     std::format("'{}' does not hold an entity", condition.Subject.Local));
                }
                else if (condition.Subject.Source == AbilityValueSource::Port)
                    CheckValue(condition.Subject, condition.Key, condition.Path, op, "the entity");
                break;
            case AbilityConditionKind::PhaseTicks:
            case AbilityConditionKind::Pulse:
                if (Phase == nullptr)
                    Report.Error("schema", condition.Key, condition.Path,
                                 "phase ticks and pulses are a phase's; this condition is outside one");
                break;
            default:
                break;
            }
            if (condition.Value.Source == AbilityValueSource::Local)
                CheckValue(condition.Value, condition.Key, condition.Path, nullptr, "the comparison");
            for (const AbilityArgumentDecl& argument : condition.Arguments)
                CheckValue(argument.Value, condition.Key, condition.Path + ".arguments." + argument.Name, op,
                           "'" + argument.Name + "'");
            for (const AbilityConditionDecl& child : condition.Children)
                CheckCondition(child, op);
        }

        void CheckOp(const AbilityOpDecl& op, std::string_view list, bool inPulse)
        {
            if (op.When)
                CheckCondition(*op.When, &op);
            const bool inHold = list == "hold";
            if (inHold != (op.Kind == AbilityOpKind::Lease))
                Report.Error("schema", op.Key, op.Path,
                             inHold ? "a phase holds only leases" : "a lease is held by a phase's 'hold' list");
            if (!op.Targets.empty())
            {
                const AbilityLocalType* type = TypeOf(op.Targets);
                if (type == nullptr || *type != AbilityLocalType::Entities)
                    Report.Error("local_type", op.Key, op.Path + ".targets",
                                 std::format("'{}' is not an entities local", op.Targets));
            }
            for (const AbilityArgumentDecl& argument : op.Arguments)
                CheckValue(argument.Value, op.Key, op.Path + ".arguments." + argument.Name, &op,
                           "'" + argument.Name + "'");
            switch (op.Kind)
            {
            case AbilityOpKind::Query:
            case AbilityOpKind::Set:
                if (TypeOf(op.Local) == nullptr)
                    Report.Error("local_unknown", op.Key, op.Path, std::format("no local '{}'", op.Local));
                if (op.Kind == AbilityOpKind::Set)
                {
                    CheckValue(op.From, op.Key, op.Path + ".from", &op, "the value");
                    const AbilityLocalType* type = TypeOf(op.Local);
                    if (type != nullptr && (*type == AbilityLocalType::Hit || *type == AbilityLocalType::Entities))
                        Report.Error("local_type", op.Key, op.Path,
                                     std::format("'{}' is written by a query, not set", op.Local));
                }
                break;
            case AbilityOpKind::Commit:
                for (const std::string& name : op.Groups)
                {
                    const auto found = Groups.find(name);
                    if (found == Groups.end())
                    {
                        Report.Error("group_unknown", op.Key, op.Path, std::format("no cost group '{}'", name));
                        continue;
                    }
                    const AbilityCostGroupDecl& group = *found->second;
                    if (group.Timing == AbilityCostTiming::Start)
                        Report.Error("cost_shape", op.Key, op.Path,
                                     std::format("'{}' is paid at start, not by a commit", name));
                    else if (inPulse && group.Repeat != AbilityCostRepeat::PerPulse)
                        Report.Error("commit_repeat", op.Key, op.Path,
                                     std::format("a commit inside 'while' pays '{}' every pulse, so it needs "
                                                 "repeat: per_pulse",
                                                 name));
                    else if (!inPulse && group.Repeat == AbilityCostRepeat::PerPulse)
                        Report.Error("commit_repeat", op.Key, op.Path,
                                     std::format("'{}' is per_pulse, so only a commit inside 'while' pays it", name));
                    else if (group.Repeat == AbilityCostRepeat::Once && !OnceCommitters.emplace(name, op.Key).second
                             && OnceCommitters[name] != op.Key)
                        Report.Warn("commit_twice", op.Key, op.Path,
                                    std::format("'{}' is paid once, but more than one commit names it", name));
                }
                break;
            case AbilityOpKind::Cue:
                if (op.At)
                    CheckValue(*op.At, op.Key, op.Path + ".at", &op, "the cue's place");
                break;
            default:
                break;
            }
        }

        void Phases()
        {
            if (Declared.Phases.size() > kAbilityPhaseCapacity)
                Report.Error("capacity", {}, "$.data.phases",
                             std::format("{} phases declared; limit {}", Declared.Phases.size(), kAbilityPhaseCapacity));
            for (std::size_t i = 0; i < Declared.Phases.size(); ++i)
                PhaseIndex.emplace(Declared.Phases[i].Key, i);
            for (const AbilityPhaseDecl& phase : Declared.Phases)
            {
                Phase = &phase;
                for (const AbilityOpDecl& op : phase.Enter)
                    CheckOp(op, "enter", false);
                if (phase.While)
                    for (const AbilityOpDecl& op : phase.While->Ops)
                        CheckOp(op, "while", true);
                if (phase.Hold.size() > kAbilityRecordLeaseCapacity)
                    Report.Error("capacity", phase.Key, phase.Path + ".hold",
                                 std::format("a phase holds at most {} leases", kAbilityRecordLeaseCapacity));
                for (const AbilityOpDecl& op : phase.Hold)
                    CheckOp(op, "hold", false);
                for (const AbilityOpDecl& op : phase.Exit)
                    CheckOp(op, "exit", false);
                for (const AbilityConditionDecl& condition : phase.Continue)
                    CheckCondition(condition, nullptr);
                if (phase.Then.empty())
                    Report.Error("no_exit", phase.Key, phase.Path,
                                 std::format("'{}' has no release, timeout, condition or end", phase.Name));
                for (const AbilityTransitionDecl& transition : phase.Then)
                {
                    if (transition.When)
                        CheckCondition(*transition.When, nullptr);
                    if (!transition.To.empty() && !PhaseIndex.contains(transition.To))
                        Report.Error("phase_unknown", transition.Key, transition.Path,
                                     std::format("no phase '{}'", transition.To));
                }
            }
            Phase = nullptr;
        }

        // Reachability over every transition, and cycles over the ones that take no
        // time: an Immediate transition is followed in the same step.
        void Graph()
        {
            const std::size_t count = Declared.Phases.size();
            if (count == 0)
                return;
            std::vector<bool> reached(count, false);
            std::vector<std::size_t> frontier{ 0 };
            reached[0] = true;
            while (!frontier.empty())
            {
                const std::size_t at = frontier.back();
                frontier.pop_back();
                for (const AbilityTransitionDecl& transition : Declared.Phases[at].Then)
                {
                    const auto next = PhaseIndex.find(transition.To);
                    if (next != PhaseIndex.end() && !reached[next->second])
                    {
                        reached[next->second] = true;
                        frontier.push_back(next->second);
                    }
                }
            }
            for (std::size_t i = 0; i < count; ++i)
                if (!reached[i])
                    Report.Warn("unreachable", Declared.Phases[i].Key, Declared.Phases[i].Path,
                                std::format("'{}' can never be entered", Declared.Phases[i].Name));

            enum class Mark : std::uint8_t { None, Open, Done };
            std::vector<Mark> marks(count, Mark::None);
            const std::function<bool(std::size_t)> cycles = [&](std::size_t at) {
                marks[at] = Mark::Open;
                for (const AbilityTransitionDecl& transition : Declared.Phases[at].Then)
                {
                    if (transition.Trigger != AbilityTransitionTrigger::Immediate)
                        continue;
                    const auto next = PhaseIndex.find(transition.To);
                    if (next == PhaseIndex.end())
                        continue;
                    if (marks[next->second] == Mark::Open)
                    {
                        Report.Error("zero_time_cycle", transition.Key, transition.Path,
                                     std::format("'{}' can return to '{}' within one tick",
                                                 Declared.Phases[at].Name, Declared.Phases[next->second].Name));
                        return true;
                    }
                    if (marks[next->second] == Mark::None && cycles(next->second))
                        return true;
                }
                marks[at] = Mark::Done;
                return false;
            };
            for (std::size_t i = 0; i < count; ++i)
                if (marks[i] == Mark::None && cycles(i))
                    break;
        }

        const AbilityDeclaration& Declared;
        Reader& Report;
        std::unordered_set<std::string> ConditionSeen;
        std::unordered_set<std::string> OpSeen;
        std::unordered_map<std::string, AbilityLocalType> LocalTypes;
        std::unordered_map<std::string, const AbilityCostGroupDecl*> Groups;
        std::unordered_map<std::string, std::string> OnceCommitters;
        std::unordered_map<std::string, std::size_t> PhaseIndex;
        const AbilityPhaseDecl* Phase = nullptr;
    };

    DataFieldSchema Field(DataFieldKind kind, std::string key, std::string label, std::string summary = {})
    {
        DataFieldSchema field = MakeDataField(kind, std::move(key), std::move(label), std::move(summary));
        field.Required = false;
        return field;
    }

    DataFieldSchema List(std::string key, std::string label, DataFieldSchema element, std::string title = "key")
    {
        DataFieldSchema list = Field(DataFieldKind::Array, std::move(key), std::move(label));
        list.Editor.Widget = "cards";
        list.Editor.TitleKey = std::move(title);
        list.Children.push_back(std::move(element));
        return list;
    }

    DataFieldSchema KeyedRecord(std::string key, std::string label)
    {
        DataFieldSchema record = Field(DataFieldKind::Record, std::move(key), std::move(label));
        DataFieldSchema stable = MakeDataField(DataFieldKind::StableKey, "key", "Key");
        record.Children.push_back(std::move(stable));
        return record;
    }

    template<typename E, std::size_t N>
    DataFieldSchema Choices(std::string key, std::string label, const std::array<Named<E>, N>& choices)
    {
        DataFieldSchema field = Field(DataFieldKind::Enum, std::move(key), std::move(label));
        for (const Named<E>& choice : choices)
            field.EnumChoices.push_back({ std::string(choice.Name), std::string(choice.Name), {} });
        return field;
    }

    // The editor's view of the format. Operation arguments and comparison values
    // are typed by the operation they feed, so the schema leaves them open and the
    // compiler checks them.
    DataFieldSchema TagList(std::string key, std::string label)
    {
        DataFieldSchema list = Field(DataFieldKind::Array, std::move(key), std::move(label));
        list.Children.push_back(MakeDataField(DataFieldKind::GameplayTag, "tag", "Tag"));
        return list;
    }

    DataFieldSchema TagQuerySchema(std::string key, std::string label)
    {
        DataFieldSchema query = Field(DataFieldKind::Record, std::move(key), std::move(label));
        query.Children = { TagList("all", "All of"), TagList("any", "Any of"), TagList("none", "None of") };
        return query;
    }

    DataFieldSchema BindingSchema(std::string key, std::string label)
    {
        DataFieldSchema binding = Field(DataFieldKind::Record, std::move(key), std::move(label));
        binding.Children.push_back(Field(DataFieldKind::String, "action", "Action"));
        binding.Children.push_back(Choices("policy", "Policy", kAbilityInputPolicyNames));
        binding.Children.push_back(Field(DataFieldKind::Int, "priority", "Priority"));
        binding.Children.push_back(Choices("consume", "Consume", kAbilityConsumeNames));
        binding.Children.push_back(Choices("when_blocked", "When refused", kAbilityWhenBlockedNames));
        binding.Children.push_back(TagQuerySchema("context", "Context"));
        binding.Children.push_back(Choices("on_interrupt", "On interruption", kAbilityInterruptNames));
        return binding;
    }

    DataSchema MakeSchema()
    {
        const auto tagList = [](std::string key, std::string label) { return TagList(std::move(key), std::move(label)); };
        DataFieldSchema tagQuery = TagQuerySchema("tags", "Tags");

        DataFieldSchema condition = KeyedRecord("condition", "Condition");
        condition.Children.push_back(tagQuery);
        condition.Children.push_back(Field(DataFieldKind::String, "hit", "Struck", "A hit local that struck something."));
        DataFieldSchema input = Field(DataFieldKind::Enum, "input", "Input");
        input.EnumChoices = { { "held", "Held", {} }, { "not_held", "Not held", {} } };
        condition.Children.push_back(std::move(input));

        DataFieldSchema op = KeyedRecord("op", "Op");
        op.Children.push_back(Choices("op", "Kind", kOpKinds));
        op.Children.push_back(Field(DataFieldKind::String, "verb", "Verb"));
        op.Children.push_back(Field(DataFieldKind::String, "query", "Query"));
        op.Children.push_back(Field(DataFieldKind::String, "lease", "Lease"));
        op.Children.push_back(Field(DataFieldKind::String, "into", "Into", "The local a query writes."));
        op.Children.push_back(Field(DataFieldKind::String, "local", "Local", "The local a set writes."));
        op.Children.push_back(Field(DataFieldKind::String, "targets", "Targets", "An entities local to repeat over."));
        op.Children.push_back(Field(DataFieldKind::GameplayTag, "cue", "Cue"));

        DataFieldSchema transition = KeyedRecord("transition", "Transition");
        transition.Children.push_back(Choices("on", "On", kTriggers));
        DataFieldSchema ticks = Field(DataFieldKind::Int, "ticks", "Ticks");
        ticks.Numeric.Minimum = 1.0;
        transition.Children.push_back(std::move(ticks));
        transition.Children.push_back(Field(DataFieldKind::String, "to", "To", "A phase's key, or 'end'."));

        DataFieldSchema timing = Field(DataFieldKind::Record, "timing", "Timing");
        timing.Children.push_back(Choices("domain", "Clock", kDomains));
        DataFieldSchema progress = Field(DataFieldKind::Int, "progress_ticks", "Progress ticks",
                                         "How many ticks phase.progress takes to reach one.");
        progress.Numeric.Minimum = 1.0;
        timing.Children.push_back(std::move(progress));

        DataFieldSchema pulse = Field(DataFieldKind::Record, "while", "Repeat");
        DataFieldSchema every = Field(DataFieldKind::Int, "every", "Every", "Ticks between pulses.");
        every.Numeric.Minimum = 1.0;
        pulse.Children.push_back(std::move(every));
        DataFieldSchema first = Field(DataFieldKind::Enum, "first", "First pulse");
        first.EnumChoices = { { "on_entry", "On entry", {} }, { "after_interval", "After one interval", {} } };
        pulse.Children.push_back(std::move(first));
        pulse.Children.push_back(List("ops", "Ops", op));

        DataFieldSchema present = KeyedRecord("present", "Presentation");
        present.Children.push_back(Field(DataFieldKind::GameplayTag, "cue", "Cue"));

        DataFieldSchema phase = KeyedRecord("phase", "Phase");
        phase.Children.push_back(Field(DataFieldKind::String, "name", "Name"));
        phase.Children.push_back(std::move(timing));
        phase.Children.push_back(List("enter", "On entry", op));
        phase.Children.push_back(std::move(pulse));
        phase.Children.push_back(List("hold", "Held", op));
        phase.Children.push_back(List("exit", "On exit", op));
        phase.Children.push_back(List("present", "Presented", present));
        phase.Children.push_back(List("continue", "Continues while", condition));
        phase.Children.push_back(List("then", "Then", transition));

        DataFieldSchema local = KeyedRecord("local", "Local");
        local.Children.push_back(Field(DataFieldKind::String, "name", "Name"));
        local.Children.push_back(Choices("type", "Type", kAbilityLocalTypeNames));

        DataFieldSchema effect = Field(DataFieldKind::Record, "effect", "Effect");
        DataFieldSchema effectRef = MakeDataField(DataFieldKind::DataAssetRef, "data", "Effect");
        effectRef.Reference.DataSubtype = "effect.definition";
        effect.Children.push_back(effectRef);

        DataFieldSchema cost = KeyedRecord("cost", "Cost group");
        cost.Children.push_back(Field(DataFieldKind::String, "name", "Name"));
        cost.Children.push_back(Choices("when", "Paid", kCostTimings));
        cost.Children.push_back(Choices("repeat", "Repeat", kCostRepeats));
        cost.Children.push_back(List("effects", "Effects", effect, "data"));

        DataFieldSchema cooldown = Field(DataFieldKind::Record, "cooldown", "Cooldown");
        DataFieldSchema cooldownEffect = effect;
        cooldownEffect.Required = true;
        cooldown.Children.push_back(std::move(cooldownEffect));
        cooldown.Children.push_back(Choices("start", "Starts", kCooldownStarts));
        cooldown.Children.push_back(Field(DataFieldKind::String, "group", "Group", "The cost group whose commit starts it."));

        DataFieldSchema binding = BindingSchema("input_default", "Input");

        DataFieldSchema display = Field(DataFieldKind::Record, "display", "Display");
        display.Children = { Field(DataFieldKind::String, "name", "Name"), Field(DataFieldKind::String, "category", "Category") };

        DataFieldSchema instances = Field(DataFieldKind::Int, "instances", "Instances", "How many may run at once.");
        instances.Numeric.Minimum = 1.0;
        instances.Numeric.Maximum = static_cast<double>(kAbilityLiveRecordCapacity);

        DataFieldSchema lifetime = Field(DataFieldKind::Record, "lifetime", "Lifetime");
        DataFieldSchema onReload = Field(DataFieldKind::Enum, "on_reload", "On reload");
        onReload.EnumChoices = { { "cancel", "Cancel", {} }, { "keep", "Keep", {} } };
        lifetime.Children.push_back(std::move(onReload));

        DataFieldSchema ownedTags = tagList("owned_tags", "Owned tags");
        DataFieldSchema cancelTags = tagList("cancel_tags", "Cancels abilities owning");

        DataSchema schema;
        schema.TypeName = std::string(kAbilityDefinitionType);
        schema.DisplayName = "Ability";
        schema.Description = "Something an actor does: when it may start, the phases it runs, and what it costs.";
        schema.AllowUnknownFields = true;
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children = {
            std::move(display),
            std::move(binding),
            List("activation", "Starts only if", condition),
            List("continuation", "Continues while", condition),
            List("cancel_on", "Cancelled when", condition),
            std::move(ownedTags),
            std::move(cancelTags),
            std::move(instances),
            List("locals", "Locals", local, "name"),
            List("costs", "Costs", cost, "name"),
            std::move(cooldown),
            List("phases", "Phases", phase, "name"),
            Choices("prediction", "Prediction", kPredictions),
            std::move(lifetime),
        };
        return schema;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        auto declaration = std::make_shared<AbilityDeclaration>();
        std::vector<AssetRef> dependencies;
        std::vector<AbilityDiagnostic> diagnostics;
        if (!CompileAbilityDeclaration(data, *declaration, dependencies, diagnostics))
        {
            const auto error = std::ranges::find(diagnostics, AbilityDiagnosticSeverity::Error,
                                                 &AbilityDiagnostic::Severity);
            return { {}, {}, std::format("{} {}", error->Path, error->Message) };
        }
        return { std::move(declaration), std::move(dependencies), {} };
    }
}

bool ParseAbilityGrantEntries(const JsonValue& list,
                              const std::string& path,
                              std::vector<AbilityGrantDeclaration>& out,
                              std::vector<AssetRef>& dependencies,
                              std::vector<AbilityDiagnostic>& diagnostics)
{
    const std::size_t before = diagnostics.size();
    Reader(diagnostics, dependencies).GrantEntries(list, path, out);
    return diagnostics.size() == before;
}

DataFieldSchema AbilityGrantEntriesSchema(std::string key, std::string label)
{
    DataFieldSchema entry = KeyedRecord("grant", "Grant");
    DataFieldSchema ability = MakeDataField(DataFieldKind::Record, "ability", "Ability");
    DataFieldSchema reference = MakeDataField(DataFieldKind::DataAssetRef, "data", "Ability");
    reference.Reference.DataSubtype = std::string(kAbilityDefinitionType);
    ability.Children.push_back(std::move(reference));
    entry.Children.push_back(std::move(ability));
    entry.Children.push_back(BindingSchema("binding", "Binding"));
    return List(std::move(key), std::move(label), std::move(entry));
}

bool CompileAbilityDeclaration(const JsonValue& data,
                               AbilityDeclaration& out,
                               std::vector<AssetRef>& dependencies,
                               std::vector<AbilityDiagnostic>& diagnostics)
{
    const std::size_t before = diagnostics.size();
    out = AbilityDeclaration{};
    Reader reader(diagnostics, dependencies);
    if (!data.IsObject())
    {
        reader.Error("schema", {}, "$.data", "an ability is an object");
        return false;
    }
    reader.Root(data, out);
    Checker(out, reader).Run();
    return std::none_of(diagnostics.begin() + static_cast<std::ptrdiff_t>(before), diagnostics.end(),
                        [](const AbilityDiagnostic& diagnostic) {
                            return diagnostic.Severity == AbilityDiagnosticSeverity::Error;
                        });
}

void RegisterAbilityDefinitionData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kAbilityDefinitionType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kAbilityDefinitionType);
}
