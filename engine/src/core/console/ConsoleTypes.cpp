#include <core/console/ConsoleTypes.h>

#include <sstream>

void ConsoleResult::Info(std::string text, std::string channel)
{
    Output.push_back({ ConsoleOutputSeverity::Info, std::move(channel), std::move(text) });
}

void ConsoleResult::Warning(std::string text, std::string channel)
{
    Output.push_back({ ConsoleOutputSeverity::Warning, std::move(channel), std::move(text) });
}

void ConsoleResult::Error(std::string text, std::string channel)
{
    Output.push_back({ ConsoleOutputSeverity::Error, std::move(channel), std::move(text) });
}

std::string ToString(const CVarValue& value)
{
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>)
            return v ? "true" : "false";
        else if constexpr (std::is_same_v<T, std::string>)
            return v;
        else
        {
            std::ostringstream out;
            out << v;
            return out.str();
        }
    }, value);
}

std::string ToString(ConsolePhase phase)
{
    switch (phase)
    {
    case ConsolePhase::EngineReady:       return "EngineReady";
    case ConsolePhase::GameLoaded:        return "GameLoaded";
    case ConsolePhase::SystemsRegistered: return "SystemsRegistered";
    case ConsolePhase::WorldReady:        return "WorldReady";
    case ConsolePhase::PlayerReady:       return "PlayerReady";
    case ConsolePhase::GameplayStarted:   return "GameplayStarted";
    }
    return "Unknown";
}

std::string ToString(ConsoleStatus status)
{
    switch (status)
    {
    case ConsoleStatus::Ok:               return "Ok";
    case ConsoleStatus::Deferred:         return "Deferred";
    case ConsoleStatus::UnknownCommand:   return "UnknownCommand";
    case ConsoleStatus::InvalidArguments: return "InvalidArguments";
    case ConsoleStatus::PhaseNotReady:    return "PhaseNotReady";
    case ConsoleStatus::ValidationFailed: return "ValidationFailed";
    case ConsoleStatus::ExecutionFailed:  return "ExecutionFailed";
    }
    return "Unknown";
}

CVarType TypeOf(const CVarValue& value)
{
    if (std::holds_alternative<bool>(value))
        return CVarType::Bool;
    if (std::holds_alternative<std::int64_t>(value))
        return CVarType::Int;
    if (std::holds_alternative<double>(value))
        return CVarType::Double;
    return CVarType::String;
}

bool IsPhaseReady(ConsolePhase current, ConsolePhase required)
{
    return static_cast<std::uint8_t>(current) >= static_cast<std::uint8_t>(required);
}
