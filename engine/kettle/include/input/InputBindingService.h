#pragma once

#include <service/IService.h>
#include <input/InputBindingTable.h>
#include <string_view>
#include <utility>

//=============================================================================
// InputBindingService
//
// Owns the compiled runtime binding data. Loaded once at startup (or on
// config reload), then read each frame by InputMappingSystem.
//
// Contains only numeric IDs and flat lookup tables — no strings in the
// runtime mapping path.
//=============================================================================
class InputBindingService : public IService
{
public:
	void SetBindings(InputBindingTable table) { Bindings = std::move(table); }
	const InputBindingTable& GetBindings() const { return Bindings; }

	// Debug helper: resolve action ID back to name (tooling only, not hot path)
	[[nodiscard]] std::string_view GetActionName(InputActionId id) const
	{
		if (id.Value == 0 || id.Value > Bindings.ActionCount) return {};
		return Bindings.ActionNames[id.Value - 1];
	}

private:
	InputBindingTable Bindings;
};
