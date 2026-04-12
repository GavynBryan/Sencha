#pragma once

#include <service/IService.h>
#include <input/InputTypes.h>
#include <vector>

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
	explicit InputStateService(uint16_t actionCount)
		: States(actionCount) {}

	struct ActionState
	{
		bool Active = false;
		float Value = 0.0f;
	};

	void SetActionState(InputActionId action, bool active, float value = 0.0f)
	{
		if (!action || action.Value >= States.size()) return;
		auto& state = States[action.Value];
		state.Active = active;
		state.Value = value;
	}

	[[nodiscard]] ActionState GetActionState(InputActionId action) const
	{
		if (!action || action.Value >= States.size()) return {};
		return States[action.Value];
	}

	[[nodiscard]] bool IsActive(InputActionId action) const
	{
		return GetActionState(action).Active;
	}

	[[nodiscard]] float GetValue(InputActionId action) const
	{
		return GetActionState(action).Value;
	}

	[[nodiscard]] uint16_t GetActionCount() const
	{
		return static_cast<uint16_t>(States.size());
	}

private:
	std::vector<ActionState> States;
};
