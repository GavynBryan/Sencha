#pragma once

#include <service/IService.h>
#include <input/InputTypes.h>
#include <array>

//=============================================================================
// InputStateService (optional)
//
// Secondary, polling-based input access. The event-driven path through
// InputEventQueueService is the primary Sencha input architecture.
//
// This service exists as convenience for cases where polling is simpler,
// e.g., checking if an action is currently held during a physics tick.
// It should not be the default way to consume input.
//
// Updated by InputMappingSystem alongside event emission.
//=============================================================================
class InputStateService : public IService
{
public:
	static constexpr uint16_t MaxActions = MaxInputActions;

	struct ActionState
	{
		bool Active = false;
		float Value = 0.0f;
	};

	void SetActionState(InputActionId action, bool active, float value = 0.0f)
	{
		if (action.Value == 0 || action.Value > MaxActions) return;
		auto& state = States[action.Value - 1];
		state.Active = active;
		state.Value = value;
	}

	[[nodiscard]] ActionState GetActionState(InputActionId action) const
	{
		if (action.Value == 0 || action.Value > MaxActions) return {};
		return States[action.Value - 1];
	}

	[[nodiscard]] bool IsActive(InputActionId action) const
	{
		return GetActionState(action).Active;
	}

	[[nodiscard]] float GetValue(InputActionId action) const
	{
		return GetActionState(action).Value;
	}

private:
	std::array<ActionState, MaxActions> States{};
};
