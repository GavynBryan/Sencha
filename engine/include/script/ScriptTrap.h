#pragma once

#include <cstdint>

// Trap catalog (docs/plans/t-language-v1-spec.md, section D.7). Codes are
// stable: they appear in logs, tests, and tooling.
enum class ScriptTrapCode : uint8_t
{
    None        = 0,
    Budget      = 1,
    CallDepth   = 2,
    DivZero     = 3,
    Conv        = 4,
    Bounds      = 5,
    Entity      = 6,
    NoComponent = 7,
    TagCapacity = 8,
    Arg         = 9,
};

[[nodiscard]] constexpr const char* ScriptTrapName(ScriptTrapCode code)
{
    switch (code)
    {
    case ScriptTrapCode::None:        return "NONE";
    case ScriptTrapCode::Budget:      return "BUDGET";
    case ScriptTrapCode::CallDepth:   return "CALL_DEPTH";
    case ScriptTrapCode::DivZero:     return "DIV_ZERO";
    case ScriptTrapCode::Conv:        return "CONV";
    case ScriptTrapCode::Bounds:      return "BOUNDS";
    case ScriptTrapCode::Entity:      return "ENTITY";
    case ScriptTrapCode::NoComponent: return "NO_COMPONENT";
    case ScriptTrapCode::TagCapacity: return "TAG_CAPACITY";
    case ScriptTrapCode::Arg:         return "ARG";
    }
    return "UNKNOWN";
}
