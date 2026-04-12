#pragma once

#include <input/InputTypes.h>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

//=============================================================================
// InputBindingTable
//
// Compiled, runtime-native binding data. All string resolution happens at
// compile time. Runtime lookup is by numeric device type + backend control
// code into flat arrays with O(1) slot access.
//
// Keyboard bindings: flat array of 512 slots indexed by control code.
// Mouse button bindings: flat array of 8 slots indexed by button code.
//
// Each slot points into a packed CompiledBinding array (start + count).
// Typical keys have 1-2 bindings, so the per-key scan is trivial.
//=============================================================================

// --- Compiled binding record -------------------------------------------------

struct CompiledBinding
{
	InputActionId Action;
	InputContextId Context;
	InputUserId User;
	InputTriggerType Trigger;
};

// --- Binding slot ------------------------------------------------------------
// Index range into a flat CompiledBinding array.

struct BindingSlot
{
	uint16_t Start = 0;
	uint16_t Count = 0;
};

// --- Table constants ---------------------------------------------------------

static constexpr uint16_t MaxKeyboardControls = 512;
static constexpr uint16_t MaxMouseButtons = 8;

// --- Compiled binding table --------------------------------------------------

struct InputBindingTable
{
	// Action name registry (indexed by ActionId.Value - 1, for debug/tooling)
	std::vector<std::string> ActionNames;
	uint16_t ActionCount = 0;

	// Keyboard: indexed by control code
	std::array<BindingSlot, MaxKeyboardControls> KeyboardSlots{};
	std::vector<CompiledBinding> KeyboardBindings;

	// Mouse buttons: indexed by button code
	std::array<BindingSlot, MaxMouseButtons> MouseButtonSlots{};
	std::vector<CompiledBinding> MouseButtonBindings;

	// -- Lookup ---------------------------------------------------------------

	[[nodiscard]] std::span<const CompiledBinding> GetKeyboardBindings(uint16_t control) const
	{
		if (control >= MaxKeyboardControls) return {};
		const auto& slot = KeyboardSlots[control];
		if (slot.Count == 0) return {};
		return std::span<const CompiledBinding>(
			KeyboardBindings.data() + slot.Start, slot.Count);
	}

	[[nodiscard]] std::span<const CompiledBinding> GetMouseButtonBindings(uint16_t button) const
	{
		if (button >= MaxMouseButtons) return {};
		const auto& slot = MouseButtonSlots[button];
		if (slot.Count == 0) return {};
		return std::span<const CompiledBinding>(
			MouseButtonBindings.data() + slot.Start, slot.Count);
	}
};
