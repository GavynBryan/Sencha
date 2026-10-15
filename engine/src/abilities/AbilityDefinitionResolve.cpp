#include <abilities/AbilityDefinitionResolve.h>

#include <abilities/AbilityGrantEntries.h>
#include <attributes/AttributeRegistry.h>
#include <authored/AuthoredLiteral.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbRegistry.h>
#include <authored/WorldVocabulary.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <format>
#include <unordered_map>
#include <utility>

namespace
{
    enum class Shape : std::uint8_t
    {
        Bool,
        Number,
        Vector,
        Entity,
        Hit,
        Entities,
    };

    const DataFieldSchema& Unwrapped(const DataFieldSchema& field)
    {
        return field.Kind == DataFieldKind::Optional && field.Children.size() == 1 ? field.Children.front() : field;
    }

    bool Fits(Shape shape, const DataFieldSchema& expected)
    {
        const DataFieldSchema& field = Unwrapped(expected);
        switch (field.Kind)
        {
        case DataFieldKind::Bool: return shape == Shape::Bool;
        case DataFieldKind::Int:
        case DataFieldKind::Float: return shape == Shape::Number;
        case DataFieldKind::Vector: return field.VectorLength == 3 && shape == Shape::Vector;
        case DataFieldKind::Entity: return shape == Shape::Entity;
        default: return false;
        }
    }

    std::optional<AbilityLocalType> LocalTypeFor(const DataFieldSchema& result)
    {
        switch (result.Kind)
        {
        case DataFieldKind::Bool: return AbilityLocalType::Bool;
        case DataFieldKind::Int: return AbilityLocalType::Int;
        case DataFieldKind::Float: return AbilityLocalType::Float;
        case DataFieldKind::Vector:
            return result.VectorLength == 3 ? std::optional(AbilityLocalType::Vec3) : std::nullopt;
        case DataFieldKind::Entity: return AbilityLocalType::Entity;
        case DataFieldKind::Array:
            return result.Children.size() == 1 && result.Children.front().Kind == DataFieldKind::Entity
                ? std::optional(AbilityLocalType::Entities)
                : std::nullopt;
        case DataFieldKind::Record:
        {
            // A hit is read by position, so the record has to be laid out as one.
            constexpr std::string_view kHit[] = { "hit", "entity", "point", "normal" };
            if (result.Children.size() != 4)
                return std::nullopt;
            for (std::size_t i = 0; i < 4; ++i)
                if (result.Children[i].Key != kHit[i])
                    return std::nullopt;
            return AbilityLocalType::Hit;
        }
        default: return std::nullopt;
        }
    }

    DataFieldSchema FieldFor(AbilityLocalType type)
    {
        DataFieldSchema field;
        switch (type)
        {
        case AbilityLocalType::Bool: field.Kind = DataFieldKind::Bool; break;
        case AbilityLocalType::Int: field.Kind = DataFieldKind::Int; break;
        case AbilityLocalType::Vec3: field.Kind = DataFieldKind::Vector; break;
        case AbilityLocalType::Entity: field.Kind = DataFieldKind::Entity; break;
        default: field.Kind = DataFieldKind::Float; break;
        }
        return field;
    }

    bool SameSource(const AbilityValueDecl& a, const AbilityValueDecl& b)
    {
        return a.Source == b.Source && a.Port == b.Port && a.Local == b.Local && a.Field == b.Field;
    }

    class Resolver
    {
    public:
        Resolver(const AbilityDeclaration& declaration, std::string_view name,
                 const AbilityResolveEnvironment& environment, AbilityDefinition& out,
                 std::vector<AbilityDiagnostic>& diagnostics)
            : Declared(declaration)
            , Name(name)
            , Env(environment)
            , Out(out)
            , Diagnostics(diagnostics)
        {
        }

        void Run()
        {
            Out = AbilityDefinition{};
            Out.DisplayName = Declared.DisplayName.empty() ? std::string(Name) : Declared.DisplayName;
            Out.InputDefault = Declared.InputDefault;
            Out.Instances = Declared.Instances;
            Out.Prediction = Declared.Prediction;
            Out.CancelOnReload = Declared.CancelOnReload;
            Out.Locals = Declared.Locals;
            for (std::size_t i = 0; i < Declared.Locals.size(); ++i)
            {
                LocalIndex.emplace(Declared.Locals[i].Key, static_cast<std::uint8_t>(i));
                if (Declared.Locals[i].Type == AbilityLocalType::Entities)
                    Out.EntitiesLocal = static_cast<std::uint8_t>(i);
            }
            Out.OwnedTags = Tags(Declared.OwnedTags, "$.data.owned_tags");
            Out.CancelTags = Tags(Declared.CancelTags, "$.data.cancel_tags");
            Costs();
            for (const AbilityConditionDecl& condition : Declared.Activation)
                Out.Activation.push_back(Condition(condition, nullptr));
            for (const AbilityConditionDecl& condition : Declared.Continuation)
                Out.Continuation.push_back(Condition(condition, nullptr));
            for (const AbilityConditionDecl& condition : Declared.CancelOn)
                Out.CancelOn.push_back(Condition(condition, nullptr));
            Phases();
        }

    private:
        void Error(std::string code, std::string_view key, std::string path, std::string message)
        {
            Diagnostics.push_back({ AbilityDiagnosticSeverity::Error, std::move(code), std::string(key), std::move(path),
                                    std::move(message) });
        }

        GameplayTagId Tag(const std::string& name, std::string_view key, const std::string& path)
        {
            const GameplayTagId tag = Env.Tags != nullptr ? Env.Tags->FindTag(name) : GameplayTagId{};
            if (!tag.IsValid())
                Error("tag_unknown", key, path, std::format("no gameplay tag '{}'", name));
            return tag;
        }

        std::vector<GameplayTagId> Tags(const std::vector<std::string>& names, const std::string& path)
        {
            std::vector<GameplayTagId> tags;
            for (const std::string& name : names)
                if (const GameplayTagId tag = Tag(name, {}, path); tag.IsValid())
                    tags.push_back(tag);
            return tags;
        }

        GameplayTagQuery TagQuery(const AbilityTagQueryDecl& declared, std::string_view key, const std::string& path)
        {
            if (Env.Tags == nullptr)
            {
                Error("tag_unknown", key, path, "this World declares no gameplay tags");
                return {};
            }
            std::vector<std::string> missing;
            GameplayTagQuery query = ResolveTagQuery(declared, *Env.Tags, missing);
            for (const std::string& name : missing)
                Error("tag_unknown", key, path, std::format("no gameplay tag '{}'", name));
            return query;
        }

        EffectId Effect(const std::string& path, std::string_view key, const std::string& where)
        {
            const EffectId effect = Env.Effects != nullptr ? Env.Effects->Find(path) : EffectId{};
            if (!effect.IsValid())
                Error("effect_unknown", key, where, std::format("no effect '{}' is declared", path));
            return effect;
        }

        void Costs()
        {
            for (std::size_t g = 0; g < Declared.Costs.size(); ++g)
            {
                const AbilityCostGroupDecl& declared = Declared.Costs[g];
                GroupIndex.emplace(declared.Key, static_cast<std::uint8_t>(g));
                AbilityCostGroup group;
                group.Key = declared.Key;
                group.Name = declared.Name.empty() ? declared.Key : declared.Name;
                group.Timing = declared.Timing;
                group.Repeat = declared.Repeat;
                for (const std::string& path : declared.Effects)
                {
                    const EffectId effect = Effect(path, declared.Key, declared.Path);
                    if (!effect.IsValid())
                        continue;
                    const EffectDefinition* definition = Env.Effects->Get(effect);
                    const bool instantAdd = definition->Duration == EffectDuration::Instant
                        && std::ranges::all_of(definition->Modifiers, [](const EffectModifier& modifier) {
                               return modifier.Op == ModifierOp::Add;
                           });
                    if (!instantAdd)
                        Error("cost_shape", declared.Key, declared.Path,
                              std::format("'{}' is not an Instant Add, so it cannot be a cost", path));
                    group.Effects.push_back(effect);
                }
                if (group.Timing == AbilityCostTiming::Start)
                    Out.StartGroups |= static_cast<std::uint8_t>(1u << g);
                Out.Costs.push_back(std::move(group));
            }
            if (Declared.Cooldown)
            {
                AbilityCooldown cooldown;
                cooldown.Effect = Effect(Declared.Cooldown->Effect, {}, "$.data.cooldown.effect");
                cooldown.Start = Declared.Cooldown->Start;
                if (const auto group = GroupIndex.find(Declared.Cooldown->Group); group != GroupIndex.end())
                    cooldown.Group = group->second;
                Out.Cooldown = cooldown;
            }
        }

        const AbilityLocalType* LocalType(const std::string& key) const
        {
            const auto found = LocalIndex.find(key);
            return found != LocalIndex.end() ? &Declared.Locals[found->second].Type : nullptr;
        }

        std::optional<Shape> ShapeOf(const AbilityValueDecl& value) const
        {
            if (value.Source == AbilityValueSource::Port)
            {
                switch (value.Port)
                {
                case AbilityPort::Actor:
                case AbilityPort::Instigator:
                case AbilityPort::Target: return Shape::Entity;
                case AbilityPort::AimOrigin:
                case AbilityPort::AimDirection: return Shape::Vector;
                default: return Shape::Number;
                }
            }
            if (value.Source != AbilityValueSource::Local)
                return std::nullopt;
            const AbilityLocalType* type = LocalType(value.Local);
            if (type == nullptr)
                return std::nullopt;
            switch (*type)
            {
            case AbilityLocalType::Bool: return Shape::Bool;
            case AbilityLocalType::Int:
            case AbilityLocalType::Float: return Shape::Number;
            case AbilityLocalType::Vec3: return Shape::Vector;
            case AbilityLocalType::Entity: return Shape::Entity;
            case AbilityLocalType::Entities: return Shape::Entities;
            case AbilityLocalType::Hit:
                return value.Field == AbilityHitField::Entity ? Shape::Entity
                     : value.Field == AbilityHitField::None   ? Shape::Hit
                                                              : Shape::Vector;
            }
            return std::nullopt;
        }

        // A value for a field of `expected`'s kind; a constant compiles against it.
        bool Value(const AbilityValueDecl& declared, const DataFieldSchema& expected, std::string_view key,
                   const std::string& path, AbilityValue& out)
        {
            out.Source = declared.Source;
            out.Port = declared.Port;
            out.Field = declared.Field;
            out.As = Unwrapped(expected).Kind;
            if (declared.Source == AbilityValueSource::Local)
                if (const auto found = LocalIndex.find(declared.Local); found != LocalIndex.end())
                    out.Local = found->second;
            if (declared.Source == AbilityValueSource::Constant)
            {
                std::vector<std::string> errors;
                if (!CompileAuthoredConstant(declared.Constant, expected, Env.Verbs, std::string(Name) + "#" + std::string(key),
                                             out.Constant, errors))
                {
                    for (std::string& error : errors)
                        Error("schema", key, path, std::move(error));
                    return false;
                }
                return true;
            }
            const std::optional<Shape> shape = ShapeOf(declared);
            if (!shape || !Fits(*shape, expected))
            {
                Error("local_type", key, path, "this value is not the kind the field takes");
                return false;
            }
            return true;
        }

        bool Guarded(const AbilityOpDecl& op, const AbilityValueDecl& value, std::string_view identity) const
        {
            if (!op.When)
                return false;
            const auto matches = [&](const AbilityConditionDecl& condition) {
                return condition.Kind == AbilityConditionKind::Entity && SameSource(condition.Subject, value)
                    && std::ranges::find(condition.Components, identity) != condition.Components.end();
            };
            return matches(*op.When)
                || (op.When->Kind == AbilityConditionKind::All && std::ranges::any_of(op.When->Children, matches));
        }

        // Query arguments in the query's declared order, defaults where omitted.
        std::vector<AbilityValue> QueryArguments(const AuthoredQueryDefinition& query,
                                                 const std::vector<AbilityArgumentDecl>& arguments,
                                                 std::string_view key, const std::string& path)
        {
            std::vector<AbilityValue> values;
            for (const AbilityArgumentDecl& argument : arguments)
                if (FindChild(query.Arguments, argument.Name) == nullptr)
                    Error("schema", key, path + ".arguments." + argument.Name,
                          std::format("'{}' takes no argument '{}'", query.Name, argument.Name));
            for (const DataFieldSchema& field : query.Arguments.Children)
            {
                AbilityValue value;
                const auto given = std::ranges::find(arguments, field.Key, &AbilityArgumentDecl::Name);
                std::vector<std::string> errors;
                if (given != arguments.end())
                    (void)Value(given->Value, field, key, path + ".arguments." + field.Key, value);
                else if (!CompileAuthoredDefault(field, std::format("ability '{}#{}'", Name, key), field.Key,
                                                 value.Constant, errors))
                    Error("schema", key, path, std::format("'{}' needs an argument '{}'", query.Name, field.Key));
                values.push_back(std::move(value));
            }
            return values;
        }

        const AuthoredQueryDefinition* FindQuery(const std::string& name, std::string_view key, const std::string& path,
                                                 AuthoredQueryHandle& handle)
        {
            handle = Env.Queries != nullptr ? Env.Queries->Resolve(name) : AuthoredQueryHandle{};
            const AuthoredQueryDefinition* query = handle.Slot.IsValid() ? Env.Queries->Get(handle.Slot) : nullptr;
            if (query == nullptr)
                Error("query_unknown", key, path, std::format("no query '{}'", name));
            return query;
        }

        std::uint16_t Condition(const AbilityConditionDecl& declared, const AbilityOpDecl* op)
        {
            AbilityCondition condition;
            condition.Key = declared.Key;
            condition.Kind = declared.Kind;
            condition.Compare = declared.Compare;
            condition.Held = declared.Held;
            DataFieldSchema number;
            number.Kind = DataFieldKind::Float;
            switch (declared.Kind)
            {
            case AbilityConditionKind::Tags:
                condition.Tags = TagQuery(declared.Tags, declared.Key, declared.Path);
                condition.HasTags = true;
                break;
            case AbilityConditionKind::Attribute:
                condition.Attribute = Env.Attributes != nullptr ? Env.Attributes->FindAttribute(declared.Name)
                                                                : AttributeId{};
                if (!condition.Attribute.IsValid())
                    Error("attribute_unknown", declared.Key, declared.Path,
                          std::format("no attribute '{}'", declared.Name));
                (void)Value(declared.Value, number, declared.Key, declared.Path, condition.Value);
                break;
            case AbilityConditionKind::Local:
            case AbilityConditionKind::PhaseTicks:
            case AbilityConditionKind::Pulse:
                (void)Value(declared.Value, number, declared.Key, declared.Path, condition.Value);
                if (declared.Kind == AbilityConditionKind::Local)
                {
                    condition.Subject.Source = AbilityValueSource::Local;
                    if (const auto found = LocalIndex.find(declared.Subject.Local); found != LocalIndex.end())
                        condition.Subject.Local = found->second;
                }
                break;
            case AbilityConditionKind::Hit:
            case AbilityConditionKind::TargetLost:
                condition.Subject.Source = AbilityValueSource::Local;
                if (const auto found = LocalIndex.find(declared.Subject.Local); found != LocalIndex.end())
                    condition.Subject.Local = found->second;
                break;
            case AbilityConditionKind::Entity:
            {
                DataFieldSchema entity;
                entity.Kind = DataFieldKind::Entity;
                (void)Value(declared.Subject, entity, declared.Key, declared.Path, condition.Subject);
                for (const std::string& identity : declared.Components)
                {
                    const ComponentId component = Env.Components != nullptr
                        ? Env.Components->GetComponentIdByType(MakeComponentTypeId(identity))
                        : InvalidComponentId;
                    if (component == InvalidComponentId)
                        Error("component_unknown", declared.Key, declared.Path,
                              std::format("no component '{}' is registered", identity));
                    else
                        condition.Components.push_back(component);
                }
                if (!declared.Tags.Empty())
                {
                    condition.Tags = TagQuery(declared.Tags, declared.Key, declared.Path);
                    condition.HasTags = true;
                }
                break;
            }
            case AbilityConditionKind::Query:
                if (const AuthoredQueryDefinition* query = FindQuery(declared.Name, declared.Key, declared.Path,
                                                                     condition.Query))
                {
                    if (query->Result.Kind != DataFieldKind::Bool)
                        Error("schema", declared.Key, declared.Path,
                              std::format("'{}' answers no yes-or-no, so it is not a condition", declared.Name));
                    condition.Arguments = QueryArguments(*query, declared.Arguments, declared.Key, declared.Path);
                }
                break;
            case AbilityConditionKind::Input:
                break;
            case AbilityConditionKind::All:
            case AbilityConditionKind::Any:
            case AbilityConditionKind::Not:
                for (const AbilityConditionDecl& child : declared.Children)
                    condition.Children.push_back(Condition(child, op));
                break;
            }
            Out.Conditions.push_back(std::move(condition));
            return static_cast<std::uint16_t>(Out.Conditions.size() - 1);
        }

        void BindOperation(const AbilityOpDecl& declared, AbilityOp& op)
        {
            const VerbRegistry* verbs = Env.Verbs.Verbs;
            const VerbId verb = verbs != nullptr ? verbs->Find(declared.Operation) : VerbId{};
            const VerbDefinition* definition = verb.IsValid() ? verbs->Get(verb) : nullptr;
            const bool lease = declared.Kind == AbilityOpKind::Lease;
            if (definition == nullptr || (definition->Kind == VerbKind::Lease) != lease)
            {
                Error(lease ? "lease_unknown" : "verb_unknown", declared.Key, declared.Path,
                      definition == nullptr ? std::format("no {} '{}'", lease ? "lease" : "verb", declared.Operation)
                                            : std::format("'{}' is {}", declared.Operation,
                                                          lease ? "an instant verb, not a lease" : "a lease, so it is held"));
                return;
            }

            VerbBindingDesc desc;
            desc.Key = std::string(Name) + "#" + declared.Key;
            desc.KeyId = MakeVerbBindingKey(desc.Key);
            desc.VerbName = declared.Operation;
            for (const AbilityArgumentDecl& argument : declared.Arguments)
            {
                VerbBindingArgument bound;
                if (argument.Value.Source == AbilityValueSource::Constant)
                {
                    bound = argument.Value.Constant;
                }
                else
                {
                    bound.Source = VerbArgumentSource::Input;
                    bound.Text = argument.Name;
                    desc.Inputs.push_back(argument.Name);
                }
                bound.Key = argument.Name;
                desc.Arguments.push_back(std::move(bound));
            }
            std::vector<std::string> errors;
            if (!CompileVerbBinding(desc, Env.Verbs, op.Binding, errors))
            {
                for (std::string& error : errors)
                    Error("schema", declared.Key, declared.Path, std::move(error));
                return;
            }

            for (const VerbCompiledInput& input : op.Binding.Inputs)
            {
                const auto argument = std::ranges::find(declared.Arguments, input.Name, &AbilityArgumentDecl::Name);
                AbilityValue value;
                for (const VerbInputDestination& destination : input.Destinations)
                {
                    const std::string path = declared.Path + ".arguments." + input.Name;
                    (void)Value(argument->Value, destination.Expected, declared.Key, path, value);
                    const std::string& identity = Unwrapped(destination.Expected).Reference.ComponentIdentity;
                    const bool fromLocal = argument->Value.Source == AbilityValueSource::Local
                        || argument->Value.Port == AbilityPort::Target;
                    if (!identity.empty() && fromLocal && Unwrapped(destination.Expected).Kind == DataFieldKind::Entity
                        && !Guarded(declared, argument->Value, identity))
                        Error("unguarded_recipient", declared.Key, path,
                              std::format("'{}' must have {}; add an 'entity' guard on it with has: [\"{}\"]",
                                          input.Name, identity, identity));
                }
                op.Inputs.push_back(std::move(value));
            }
        }

        std::uint16_t Op(const AbilityOpDecl& declared)
        {
            AbilityOp op;
            op.Key = declared.Key;
            op.Kind = declared.Kind;
            if (declared.When)
                op.When = Condition(*declared.When, &declared);
            if (!declared.Targets.empty())
                if (const auto found = LocalIndex.find(declared.Targets); found != LocalIndex.end())
                    op.Targets = found->second;
            if (const auto found = LocalIndex.find(declared.Local); found != LocalIndex.end())
                op.Local = found->second;

            switch (declared.Kind)
            {
            case AbilityOpKind::Invoke:
            case AbilityOpKind::Lease:
                BindOperation(declared, op);
                break;
            case AbilityOpKind::Query:
                if (const AuthoredQueryDefinition* query = FindQuery(declared.Operation, declared.Key, declared.Path,
                                                                     op.Query))
                {
                    op.Arguments = QueryArguments(*query, declared.Arguments, declared.Key, declared.Path);
                    const std::optional<AbilityLocalType> answer = LocalTypeFor(query->Result);
                    const AbilityLocalType* into = LocalType(declared.Local);
                    const auto numeric = [](AbilityLocalType type) {
                        return type == AbilityLocalType::Int || type == AbilityLocalType::Float;
                    };
                    if (into != nullptr && (!answer || (*answer != *into && !(numeric(*answer) && numeric(*into)))))
                        Error("local_type", declared.Key, declared.Path,
                              std::format("'{}' answers a value '{}' cannot hold", declared.Operation, declared.Local));
                }
                break;
            case AbilityOpKind::Set:
                if (const AbilityLocalType* type = LocalType(declared.Local))
                    (void)Value(declared.From, FieldFor(*type), declared.Key, declared.Path + ".from", op.From);
                break;
            case AbilityOpKind::Commit:
                for (const std::string& group : declared.Groups)
                    if (const auto found = GroupIndex.find(group); found != GroupIndex.end())
                        op.Groups |= static_cast<std::uint8_t>(1u << found->second);
                op.ContinueOnFail = declared.ContinueOnFail;
                break;
            case AbilityOpKind::Cue:
                op.Cue = Tag(declared.Cue, declared.Key, declared.Path);
                if (declared.At)
                {
                    AbilityValue at;
                    at.Source = declared.At->Source;
                    at.Port = declared.At->Port;
                    at.Field = declared.At->Field;
                    at.As = DataFieldKind::Vector;
                    if (const auto found = LocalIndex.find(declared.At->Local); found != LocalIndex.end())
                        at.Local = found->second;
                    const std::optional<Shape> shape = ShapeOf(*declared.At);
                    if (!shape || (*shape != Shape::Vector && *shape != Shape::Hit))
                        Error("local_type", declared.Key, declared.Path + ".at", "a cue is placed at a point or a hit");
                    op.At = std::move(at);
                }
                break;
            }
            Out.Ops.push_back(std::move(op));
            return static_cast<std::uint16_t>(Out.Ops.size() - 1);
        }

        std::vector<std::uint16_t> Ops(const std::vector<AbilityOpDecl>& declared)
        {
            std::vector<std::uint16_t> ops;
            for (const AbilityOpDecl& op : declared)
                ops.push_back(Op(op));
            return ops;
        }

        [[nodiscard]] bool HoldsCompleting(const AbilityPhase& phase) const
        {
            const VerbRegistry* verbs = Env.Verbs.Verbs;
            return verbs != nullptr && std::ranges::any_of(phase.Hold, [&](std::uint16_t op) {
                const VerbId verb = Out.Ops[op].Binding.Verb;
                return verb.IsValid() && verbs->IsLive(verb) && verbs->Get(verb)->Completes;
            });
        }

        void Phases()
        {
            std::unordered_map<std::string, std::uint8_t> index;
            for (std::size_t i = 0; i < Declared.Phases.size(); ++i)
                index.emplace(Declared.Phases[i].Key, static_cast<std::uint8_t>(i));
            for (const AbilityPhaseDecl& declared : Declared.Phases)
            {
                AbilityPhase phase;
                phase.Key = declared.Key;
                phase.Name = declared.Name.empty() ? declared.Key : declared.Name;
                phase.Domain = declared.Domain;
                phase.ProgressTicks = declared.ProgressTicks;
                phase.Hold = Ops(declared.Hold);
                phase.Enter = Ops(declared.Enter);
                if (declared.While)
                {
                    phase.Pulses = true;
                    phase.PulseEvery = declared.While->Every;
                    phase.PulseFirstOnEntry = declared.While->FirstOnEntry;
                    phase.PulseOps = Ops(declared.While->Ops);
                    for (const std::uint16_t op : phase.PulseOps)
                        Out.Ops[op].Pulsed = true;
                }
                phase.Exit = Ops(declared.Exit);
                for (const AbilityPresentDecl& present : declared.Present)
                    if (const GameplayTagId cue = Tag(present.Cue, present.Key, declared.Path); cue.IsValid())
                        phase.Present.push_back(cue);
                for (const AbilityConditionDecl& condition : declared.Continue)
                    phase.Continue.push_back(Condition(condition, nullptr));
                for (const AbilityTransitionDecl& declaredTransition : declared.Then)
                {
                    AbilityTransition transition;
                    transition.Key = declaredTransition.Key;
                    transition.Trigger = declaredTransition.Trigger;
                    transition.Ticks = declaredTransition.Ticks;
                    if (declaredTransition.When)
                        transition.When = Condition(*declaredTransition.When, nullptr);
                    if (const auto to = index.find(declaredTransition.To); to != index.end())
                        transition.To = to->second;
                    if (transition.Trigger == AbilityTransitionTrigger::Complete && !HoldsCompleting(phase))
                        Error("complete_unheld", declaredTransition.Key, declaredTransition.Path,
                              std::format("'{}' holds nothing that completes on its own, so 'on complete' would "
                                          "never come; hold a timed burst, a tether or another lease that does",
                                          phase.Name));
                    phase.Then.push_back(std::move(transition));
                }
                Out.Phases.push_back(std::move(phase));
            }
        }

        const AbilityDeclaration& Declared;
        std::string_view Name;
        const AbilityResolveEnvironment& Env;
        AbilityDefinition& Out;
        std::vector<AbilityDiagnostic>& Diagnostics;
        std::unordered_map<std::string, std::uint8_t> LocalIndex;
        std::unordered_map<std::string, std::uint8_t> GroupIndex;
    };
}

AbilityResolveEnvironment MakeAbilityResolveEnvironment(const World& world)
{
    AbilityResolveEnvironment environment;
    environment.Tags = world.TryGetResource<GameplayTagRegistry>();
    environment.Attributes = world.TryGetResource<AttributeRegistry>();
    environment.Effects = world.TryGetResource<EffectRegistry>();
    environment.Queries = FindAuthoredQueryRegistry(world);
    environment.Verbs = MakeVerbBindingEnvironment(world);
    environment.Components = &world;
    return environment;
}

bool ResolveAbilityDeclaration(const AbilityDeclaration& declaration,
                               std::string_view name,
                               const AbilityResolveEnvironment& environment,
                               AbilityDefinition& out,
                               std::vector<AbilityDiagnostic>& diagnostics)
{
    const std::size_t before = diagnostics.size();
    Resolver(declaration, name, environment, out, diagnostics).Run();
    return std::none_of(diagnostics.begin() + static_cast<std::ptrdiff_t>(before), diagnostics.end(),
                        [](const AbilityDiagnostic& diagnostic) {
                            return diagnostic.Severity == AbilityDiagnosticSeverity::Error;
                        });
}
