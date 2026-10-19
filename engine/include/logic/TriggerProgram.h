#pragma once

#include <authored/AuthoredQueryBinding.h>
#include <ecs/ComponentTypeId.h>
#include <logic/Trigger.h>

enum class TriggerInput : std::uint8_t
{
    Trigger,
    Activator,
    Event,
    Tick,
    Up,
    Forward,
    Right
};
struct TriggerCommand
{
    CompiledVerbBinding Binding;
    std::vector<TriggerInput> Inputs;
};
struct TriggerCondition
{
    CompiledAuthoredQueryBinding Binding;
    std::vector<TriggerInput> Inputs;
};
struct CompiledTriggerProgram
{
    std::vector<ComponentTypeId> RequiredComponents;
    std::vector<TriggerCondition> Conditions;
    std::vector<TriggerCommand> Commands;
    std::vector<AssetRef> Dependencies;
};

inline constexpr std::size_t kTriggerCommandLimit = 64;
inline constexpr std::size_t kTriggerConditionLimit = 32;

[[nodiscard]] JsonValue EmptyTriggerProgram();
[[nodiscard]] bool ValidateTriggerActivation(const Trigger &trigger, std::vector<std::string> &errors);
[[nodiscard]] bool CompileTriggerProgram(const JsonValue &source, const VerbBindingEnvironment &environment,
                                         const AuthoredQueryRegistry *queries, CompiledTriggerProgram &out,
                                         std::vector<std::string> &errors);
