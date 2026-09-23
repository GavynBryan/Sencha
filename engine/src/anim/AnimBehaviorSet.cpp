#include <anim/AnimBehaviorSet.h>

#include "AnimSchemaFields.h"

#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <optional>

namespace
{
    constexpr std::array<std::string_view, 4> kKindNames{ "cyclic", "one_shot", "flow", "hold" };
    constexpr std::array<std::string_view, 3> kBlendNames{ "inertialize", "crossfade", "snap" };
    constexpr std::array<std::string_view, 2> kPhaseNames{ "reset", "carry" };
    constexpr std::array<std::string_view, 3> kLatchNames{ "none", "until_complete", "until_request_ends" };
    constexpr std::array<std::string_view, 3> kInterruptNames{ "priority_at_least", "tags", "never" };
    constexpr std::array<std::string_view, 2> kOnInterruptNames{ "abort", "cancel_section" };
    constexpr std::array<std::string_view, 3> kOnCancelNames{ "finish", "abort", "cancel_section" };
    constexpr std::array<std::string_view, 3> kLateJoinNames{ "skip", "snap_to_end", "reconstruct" };
    constexpr std::array<std::string_view, 2> kScopeNames{ "cosmetic", "gameplay" };

    template <std::size_t N>
    std::size_t IndexOf(const std::array<std::string_view, N>& names, std::string_view value)
    {
        const auto it = std::find(names.begin(), names.end(), value);
        return it == names.end() ? 0 : static_cast<std::size_t>(it - names.begin());
    }

    template <std::size_t N>
    std::vector<DataEnumChoice> Choices(const std::array<std::string_view, N>& names,
                                        std::initializer_list<const char*> labels)
    {
        std::vector<DataEnumChoice> choices;
        auto label = labels.begin();
        for (std::string_view name : names)
            choices.push_back({ std::string(name), label != labels.end() ? *label++ : std::string(name), {} });
        return choices;
    }

    DataSchema MakeSchema()
    {
        using AnimSchema::ArrayOf;
        using AnimSchema::DataRef;
        using AnimSchema::Enum;
        using AnimSchema::Field;
        using AnimSchema::Record;
        const auto optional = [](DataFieldSchema field) {
            field.Required = false;
            return field;
        };

        const auto lifecycle = [&](std::string key, std::string label, std::string description) {
            return Record(std::move(key), std::move(label), std::move(description),
                {
                    Field("binding", DataFieldKind::String, "Binding",
                          "The authored binding key; it receives the behavior's tag as its 'behavior' input."),
                    optional(Enum("scope", "Scope", "Cosmetic where a pose is presented, gameplay on the authority.",
                                  Choices(kScopeNames, { "Cosmetic", "Gameplay" }))),
                },
                false);
        };

        DataFieldSchema blend = AnimBlendPolicySchema("blend", "Blend", "How a change to this behavior is absorbed.");

        DataFieldSchema latchTags = ArrayOf("tags", "Interrupting behaviors",
                                            "Behaviors that may pre-empt the latch.",
                                            Field({}, DataFieldKind::GameplayTag, "Behavior", {}));
        DataFieldSchema latch = Record("latch", "Latch", "Extends the winner's stay; never chooses one.",
            {
                optional(Enum("mode", "Mode", "What holds the behavior once it wins.",
                              Choices(kLatchNames, { "None", "Until complete", "Until request ends" }))),
                optional(Enum("interruptible_by", "Interruptible by", "What may pre-empt it.",
                              Choices(kInterruptNames, { "Priority at least", "Tags", "Never" }))),
                optional(Field("priority", DataFieldKind::Int, "Priority band",
                               "A rule's priority must reach this to pre-empt.")),
                std::move(latchTags),
                optional(Enum("on_interrupt", "On interrupt", "Cancel section applies to flows only.",
                              Choices(kOnInterruptNames, { "Abort", "Cancel section" }))),
                optional(Enum("on_request_cancel", "On request cancel", "When the latching request ends.",
                              Choices(kOnCancelNames, { "Finish", "Abort", "Cancel section" }))),
            },
            false);

        DataFieldSchema behavior = Record({}, "Behavior", {},
            {
                Field("tag", DataFieldKind::GameplayTag, "Tag", "The behavior's tag, e.g. Anim.Action.Reload."),
                Enum("kind", "Kind", "How its content plays.",
                     Choices(kKindNames, { "Cyclic", "One-shot", "Flow", "Hold" })),
                std::move(blend),
                std::move(latch),
                optional(Enum("late_join", "Late join", "What a late joiner sees.",
                              Choices(kLateJoinNames, { "Skip", "Snap to end", "Reconstruct" }))),
                optional(Field("sync_group", DataFieldKind::GameplayTag, "Sync group",
                               "Behaviors that keep phase with each other.")),
                optional(Field("root_motion", DataFieldKind::Bool, "Root motion",
                               "Contributes a motion source; requires request anchoring.")),
                optional(Field("event_weight", DataFieldKind::Float, "Event weight",
                               "Layer weight below which cosmetic events do not fire.")),
                lifecycle("on_entered", "On entered", "Invoked when a layer enters this behavior."),
                lifecycle("on_exited", "On exited", "Invoked when a layer leaves this behavior."),
            });
        DataFieldSchema behaviors = ArrayOf("behaviors", "Behaviors", "Declared by tag.", std::move(behavior), true);
        behaviors.Editor.Widget = "cards";
        behaviors.Editor.TitleKey = "tag";

        DataSchema schema;
        schema.TypeName = std::string(kAnimBehaviorSetType);
        schema.DisplayName = "Animation behavior set";
        schema.Description = "Behavior tags and how each one plays, blends, latches and late-joins.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children.push_back(std::move(behaviors));
        return schema;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        DataAssetCompileResult result;
        auto set = std::make_shared<AnimBehaviorSet>();
        GameplayTagRegistry tagSyntax;

        const auto text = [](const JsonValue* object, std::string_view key) -> std::optional<std::string> {
            const JsonValue* value = object != nullptr ? object->Find(key) : nullptr;
            return value != nullptr && value->IsString() ? std::optional(value->AsString()) : std::nullopt;
        };
        const auto number = [](const JsonValue* object, std::string_view key, double fallback) {
            const JsonValue* value = object != nullptr ? object->Find(key) : nullptr;
            return value != nullptr && value->IsNumber() ? value->AsNumber() : fallback;
        };

        const JsonValue::Array& behaviors = data.Find("behaviors")->AsArray();
        for (std::size_t i = 0; i < behaviors.size(); ++i)
        {
            const JsonValue& entry = behaviors[i];
            const std::string at = std::format("$.data.behaviors[{}]", i);
            AnimBehaviorDecl behavior;
            behavior.Tag = entry.Find("tag")->AsString();
            GameplayTagError error;
            if (!tagSyntax.RegisterTag(behavior.Tag, &error))
            {
                result.Error = at + ".tag " + error.Message;
                return result;
            }
            if (std::any_of(set->Behaviors.begin(), set->Behaviors.end(),
                            [&](const AnimBehaviorDecl& other) { return other.Tag == behavior.Tag; }))
            {
                result.Error = at + ".tag '" + behavior.Tag + "' is declared twice in this set.";
                return result;
            }
            behavior.Kind = static_cast<AnimBehaviorKind>(IndexOf(kKindNames, entry.Find("kind")->AsString()));

            if (!ReadAnimBlendPolicy(entry.Find("blend"), at + ".blend", behavior.Blend, result.Error))
                return result;

            const JsonValue* latch = entry.Find("latch");
            if (std::optional<std::string> mode = text(latch, "mode"))
                behavior.Latch.Mode = static_cast<AnimLatchMode>(IndexOf(kLatchNames, *mode));
            if (std::optional<std::string> by = text(latch, "interruptible_by"))
                behavior.Latch.InterruptibleBy = static_cast<AnimInterruptKind>(IndexOf(kInterruptNames, *by));
            behavior.Latch.Priority = static_cast<std::int32_t>(number(latch, "priority", behavior.Latch.Priority));
            if (const JsonValue* tags = latch != nullptr ? latch->Find("tags") : nullptr; tags != nullptr)
            {
                for (const JsonValue& tag : tags->AsArray())
                    behavior.Latch.Tags.push_back(tag.AsString());
            }
            if (std::optional<std::string> onInterrupt = text(latch, "on_interrupt"))
                behavior.Latch.OnInterrupt =
                    static_cast<AnimInterruptAction>(IndexOf(kOnInterruptNames, *onInterrupt));
            if (std::optional<std::string> onCancel = text(latch, "on_request_cancel"))
                behavior.Latch.OnRequestCancel =
                    static_cast<AnimRequestCancelAction>(IndexOf(kOnCancelNames, *onCancel));

            if (std::optional<std::string> lateJoin = text(&entry, "late_join"))
                behavior.LateJoin = static_cast<AnimLateJoin>(IndexOf(kLateJoinNames, *lateJoin));
            if (std::optional<std::string> group = text(&entry, "sync_group"))
                behavior.SyncGroup = *group;
            if (const JsonValue* root = entry.Find("root_motion"); root != nullptr && root->IsBool())
                behavior.RootMotion = root->AsBool();
            behavior.EventWeight = static_cast<float>(number(&entry, "event_weight", behavior.EventWeight));
            const auto lifecycle = [&](std::string_view key) -> std::optional<AnimLifecycleDecl> {
                const JsonValue* record = entry.Find(key);
                if (record == nullptr || !record->IsObject())
                    return std::nullopt;
                AnimLifecycleDecl decl;
                decl.Binding = text(record, "binding").value_or(std::string{});
                if (std::optional<std::string> scope = text(record, "scope"))
                    decl.Scope = static_cast<AnimEventScope>(IndexOf(kScopeNames, *scope));
                return decl;
            };
            behavior.OnEntered = lifecycle("on_entered");
            behavior.OnExited = lifecycle("on_exited");
            for (const auto& [key, decl] : { std::pair{ "on_entered", &behavior.OnEntered },
                                             std::pair{ "on_exited", &behavior.OnExited } })
            {
                if (decl->has_value() && (*decl)->Binding.empty())
                {
                    result.Error = std::format("{}.{}.binding A lifecycle event names its binding.", at, key);
                    return result;
                }
            }

            const bool flow = behavior.Kind == AnimBehaviorKind::Flow;
            if (!flow && (behavior.Latch.OnInterrupt == AnimInterruptAction::CancelSection
                          || behavior.Latch.OnRequestCancel == AnimRequestCancelAction::CancelSection))
            {
                result.Error = at + ".latch Only a flow has a cancel section to go to.";
                return result;
            }
            if (behavior.Kind == AnimBehaviorKind::Cyclic && behavior.Latch.Mode == AnimLatchMode::UntilComplete)
            {
                result.Error = at + ".latch.mode Cyclic content never completes, so it cannot latch until it does.";
                return result;
            }
            if (behavior.Latch.Mode != AnimLatchMode::None
                && behavior.Latch.InterruptibleBy == AnimInterruptKind::Tags && behavior.Latch.Tags.empty())
            {
                result.Error = at + ".latch.tags A latch interruptible by tags names at least one.";
                return result;
            }
            if (behavior.Blend.InMs < 0.0f || behavior.Blend.OutMs < 0.0f)
            {
                result.Error = at + ".blend A blend duration is not negative.";
                return result;
            }
            set->Behaviors.push_back(std::move(behavior));
        }
        result.Value = std::move(set);
        return result;
    }
}

DataFieldSchema AnimBlendPolicySchema(std::string key, std::string label, std::string summary)
{
    using AnimSchema::Enum;
    using AnimSchema::Field;
    using AnimSchema::Record;
    const auto optional = [](DataFieldSchema field) {
        field.Required = false;
        return field;
    };
    DataFieldSchema inMs = Field("in_ms", DataFieldKind::Float, "In (ms)", "How long the change takes to absorb.");
    inMs.Numeric.Minimum = 0.0;
    inMs.Units = "ms";
    DataFieldSchema outMs = Field("out_ms", DataFieldKind::Float, "Out (ms)",
                                  "Crossfade only: how long the outgoing content stays alive. None uses In.");
    outMs.Numeric.Minimum = 0.0;
    outMs.Units = "ms";
    return Record(std::move(key), std::move(label), std::move(summary),
        {
            optional(Enum("in", "In", "Inertialize by default; crossfade keeps both poses alive.",
                          Choices(kBlendNames, { "Inertialize", "Crossfade", "Snap" }))),
            optional(std::move(inMs)),
            optional(std::move(outMs)),
            optional(Enum("phase", "Phase", "Carry aligns normalized time within a sync group.",
                          Choices(kPhaseNames, { "Reset", "Carry" }))),
        },
        false);
}

bool ReadAnimBlendPolicy(const JsonValue* blend, const std::string& at, AnimBlendPolicy& out, std::string& error)
{
    if (blend == nullptr || !blend->IsObject())
        return true;
    const auto text = [&](std::string_view key) -> const std::string* {
        const JsonValue* value = blend->Find(key);
        return value != nullptr && value->IsString() ? &value->AsString() : nullptr;
    };
    const auto number = [&](std::string_view key, float fallback) {
        const JsonValue* value = blend->Find(key);
        return value != nullptr && value->IsNumber() ? static_cast<float>(value->AsNumber()) : fallback;
    };
    if (const std::string* in = text("in"))
        out.In = static_cast<AnimBlendMode>(IndexOf(kBlendNames, *in));
    out.InMs = number("in_ms", out.InMs);
    out.OutMs = number("out_ms", out.OutMs);
    if (const std::string* phase = text("phase"))
        out.Phase = static_cast<AnimPhasePolicy>(IndexOf(kPhaseNames, *phase));
    if (out.InMs < 0.0f || out.OutMs < 0.0f || !std::isfinite(out.InMs) || !std::isfinite(out.OutMs))
    {
        error = at + " Blend durations are finite and not negative.";
        return false;
    }
    return true;
}

std::string_view AnimBlendModeName(AnimBlendMode mode)
{
    return kBlendNames[static_cast<std::size_t>(mode)];
}

std::string_view AnimBehaviorKindName(AnimBehaviorKind kind)
{
    return kKindNames[static_cast<std::size_t>(kind)];
}

std::string_view AnimLatchModeName(AnimLatchMode mode)
{
    return kLatchNames[static_cast<std::size_t>(mode)];
}

void RegisterAnimBehaviorSet(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kAnimBehaviorSetType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kAnimBehaviorSetType);
}
