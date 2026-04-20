#pragma once

#include <core/service/IService.h>
#include <platform/InputBindingTable.h>
#include <string_view>
#include <utility>

//=============================================================================
// InputBindingService
//
// Owns the compiled runtime binding data. Loaded once at startup (or on
// config reload), then read each frame by InputMappingSystem.
//
// Contains only numeric IDs and flat lookup tables â€” no strings in the
// runtime mapping path. Action names are retained only for debug/tooling
// helpers, not runtime mapping decisions.
//=============================================================================
class InputBindingService : public IService
{
public:
	void SetBindings(InputBindingTable table) { Bindings = std::move(table); }
	const InputBindingTable& GetBindings() const { return Bindings; }

	// Debug helper: resolve action ID back to name (tooling only, not hot path)
	[[nodiscard]] std::string_view GetActionName(InputActionId id) const
	{
		if (!id || id.Value >= Bindings.ActionCount) return {};
		return Bindings.ActionNames[id.Value];
	}

private:
	InputBindingTable Bindings;
};
