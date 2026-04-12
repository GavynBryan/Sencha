#pragma once

#include <cstdint>
#include <functional>

//=============================================================================
// Input runtime types
//
// Compact, numeric types for the input pipeline. No strings in hot paths.
// IDs start at 1; zero means invalid/unset.
//=============================================================================

// --- Identity types ----------------------------------------------------------

struct InputActionId
{
	uint16_t Value = 0;

	bool operator==(const InputActionId&) const = default;
	auto operator<=>(const InputActionId&) const = default;
	explicit operator bool() const { return Value != 0; }
};

template<> struct std::hash<InputActionId>
{
	std::size_t operator()(const InputActionId& id) const noexcept
	{
		return std::hash<uint16_t>{}(id.Value);
	}
};

struct InputUserId
{
	uint8_t Value = 0;

	bool operator==(const InputUserId&) const = default;
	auto operator<=>(const InputUserId&) const = default;
	explicit operator bool() const { return Value != 0; }
};

struct InputContextId
{
	uint8_t Value = 0;

	bool operator==(const InputContextId&) const = default;
	auto operator<=>(const InputContextId&) const = default;
	explicit operator bool() const { return Value != 0; }
};

// --- Enums -------------------------------------------------------------------

enum class InputDeviceType : uint8_t
{
	Keyboard,
	Mouse,
	Controller,
	Count
};

enum class InputPhase : uint8_t
{
	Started,     // Action just began (button pressed, threshold crossed)
	Performed,   // Action is ongoing (held button each frame)
	Canceled     // Action ended (button released)
};

enum class InputTriggerType : uint8_t
{
	Pressed,     // Fire Started on press
	Released,    // Fire Started on release
	Held         // Press -> Started, each frame -> Performed, release -> Canceled
};

// --- Mouse control constants -------------------------------------------------
// Engine-owned mouse button and wheel codes.

namespace MouseControl
{
	constexpr uint16_t Left     = 1;
	constexpr uint16_t Middle   = 2;
	constexpr uint16_t Right    = 3;
	constexpr uint16_t X1       = 4;
	constexpr uint16_t X2       = 5;
	constexpr uint16_t WheelUp  = 6;
	constexpr uint16_t WheelDown = 7;
}

// --- Raw input event ---------------------------------------------------------
// Produced by backend ingest, consumed by InputMappingSystem.
// Compact, no strings, no heap allocation.

struct RawInputEvent
{
	InputDeviceType Device = InputDeviceType::Keyboard;
	bool Pressed = false;
	uint16_t Control = 0;   // Backend-resolved keyboard/mouse control code
	float Value = 0.0f;     // 1.0 for digital press, analog value for axes/wheel
	InputUserId User;
};

// --- Semantic action event ---------------------------------------------------
// Produced by InputMappingSystem, consumed by gameplay systems.

struct InputActionEvent
{
	InputActionId Action;
	InputPhase Phase = InputPhase::Started;
	float Value = 0.0f;
	InputUserId User;
	InputContextId Context;
};
