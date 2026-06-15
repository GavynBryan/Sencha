#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class ConsoleRegistry;

enum class ConsolePhase : std::uint8_t
{
    EngineReady,
    GameLoaded,
    SystemsRegistered,
    WorldReady,
    PlayerReady,
    GameplayStarted,
};

enum class CVarType : std::uint8_t
{
    Bool,
    Int,
    Double,
    String,
};

enum class CVarFlags : std::uint32_t
{
    None      = 0,
    ReadOnly  = 1u << 0,
    InitOnly   = 1u << 1,
    Archive    = 1u << 2,
    Cheat      = 1u << 3,
    Developer  = 1u << 4,
    Hidden     = 1u << 5,
    Transient  = 1u << 6,
    Latched    = 1u << 7,
    Unsafe     = 1u << 8,
};

inline CVarFlags operator|(CVarFlags a, CVarFlags b)
{
    return static_cast<CVarFlags>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline CVarFlags& operator|=(CVarFlags& a, CVarFlags b)
{
    a = a | b;
    return a;
}

inline bool HasFlag(CVarFlags flags, CVarFlags flag)
{
    return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0;
}

enum class ConsoleCommandFlags : std::uint32_t
{
    None      = 0,
    Developer = 1u << 0,
    Hidden    = 1u << 1,
    Unsafe    = 1u << 2,
};

inline ConsoleCommandFlags operator|(ConsoleCommandFlags a, ConsoleCommandFlags b)
{
    return static_cast<ConsoleCommandFlags>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline bool HasFlag(ConsoleCommandFlags flags, ConsoleCommandFlags flag)
{
    return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0;
}

using CVarValue = std::variant<bool, std::int64_t, double, std::string>;

struct ConsoleValueSource
{
    std::string Description = "default";
    std::string File;
    int Line = 0;
    int Column = 0;
};

enum class ConsoleOutputSeverity : std::uint8_t
{
    Info,
    Warning,
    Error,
};

struct ConsoleOutputEntry
{
    ConsoleOutputSeverity Severity = ConsoleOutputSeverity::Info;
    std::string Channel = "console";
    std::string Text;
};

enum class ConsoleStatus : std::uint8_t
{
    Ok,
    Deferred,
    UnknownCommand,
    InvalidArguments,
    PhaseNotReady,
    ValidationFailed,
    ExecutionFailed,
};

struct ConsoleResult
{
    ConsoleStatus Status = ConsoleStatus::Ok;
    std::vector<ConsoleOutputEntry> Output;

    [[nodiscard]] bool Succeeded() const { return Status == ConsoleStatus::Ok; }

    void Info(std::string text, std::string channel = "console");
    void Warning(std::string text, std::string channel = "console");
    void Error(std::string text, std::string channel = "console");
};

struct CVarValidationResult
{
    bool Accepted = true;
    std::string Message;

    static CVarValidationResult Ok() { return {}; }
    static CVarValidationResult Reject(std::string message)
    {
        return CVarValidationResult{ false, std::move(message) };
    }
};

struct CVarChangeContext
{
    std::string_view Name;
    const CVarValue& OldValue;
    const CVarValue& NewValue;
    const ConsoleValueSource& Source;
    ConsolePhase Phase = ConsolePhase::EngineReady;
};

using CVarValidator = std::function<CVarValidationResult(const CVarValue&)>;
using CVarChangeCallback = std::function<void(const CVarChangeContext&)>;

struct CVarMetadata
{
    std::string Name;
    std::string Owner = "engine";
    CVarType Type = CVarType::String;
    CVarValue DefaultValue = std::string{};
    CVarValue CurrentValue = std::string{};
    std::optional<CVarValue> PendingValue;
    std::optional<CVarValue> LatchedValue;
    CVarFlags Flags = CVarFlags::None;
    std::string Help;
    ConsoleValueSource Source;
    std::optional<double> Min;
    std::optional<double> Max;
    std::vector<std::string> EnumValues;
    CVarValidator Validator;
    CVarChangeCallback OnChange;
};

struct PendingCVarAssignment
{
    std::string Name;
    std::string ValueText;
    ConsoleValueSource Source;
    std::string Diagnostic;
    bool Resolved = false;
};

struct ConsoleExecutionContext
{
    ConsoleRegistry& Registry;
    ConsolePhase Phase = ConsolePhase::EngineReady;
    bool Deferrable = false;
};

using ConsoleCommandCallback =
    std::function<ConsoleResult(ConsoleExecutionContext&, std::span<const std::string>)>;

struct ConsoleCommandMetadata
{
    std::string Name;
    std::string Owner = "engine";
    std::string Usage;
    std::string Help;
    std::vector<std::string> Examples;
    ConsoleCommandFlags Flags = ConsoleCommandFlags::None;
    ConsolePhase RequiredPhase = ConsolePhase::EngineReady;
    ConsoleCommandCallback Callback;
};

[[nodiscard]] std::string ToString(const CVarValue& value);
[[nodiscard]] std::string ToString(ConsolePhase phase);
[[nodiscard]] std::string ToString(ConsoleStatus status);
[[nodiscard]] CVarType TypeOf(const CVarValue& value);
[[nodiscard]] bool IsPhaseReady(ConsolePhase current, ConsolePhase required);

