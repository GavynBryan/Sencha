#include <input/InputMappingSystem.h>
#include <input/RawInputBufferService.h>
#include <input/InputBindingService.h>
#include <input/InputEventQueueService.h>
#include <input/InputStateService.h>
#include <input/InputBindingTable.h>

InputMappingSystem::InputMappingSystem(
	LoggingProvider& logging,
	RawInputBufferService& rawInput,
	InputBindingService& bindings,
	InputEventQueueService& actionQueue,
	InputStateService* stateService)
	: Log(logging.GetLogger<InputMappingSystem>())
	, RawInput(rawInput)
	, Bindings(bindings)
	, ActionQueue(actionQueue)
	, State(stateService)
{
}

void InputMappingSystem::Update()
{
	ActionQueue.GetBuffer().Clear();

	for (const auto& raw : RawInput.GetBuffer().Items())
	{
		ProcessRawEvent(raw);
	}

	EmitHeldPerformed();
}

void InputMappingSystem::ProcessRawEvent(const RawInputEvent& raw)
{
	const auto& table = Bindings.GetBindings();

	std::span<const CompiledBinding> bindings;

	switch (raw.Device)
	{
	case InputDeviceType::Keyboard:
		bindings = table.GetKeyboardBindings(raw.Control);
		break;
	case InputDeviceType::Mouse:
		bindings = table.GetMouseButtonBindings(raw.Control);
		break;
	default:
		return;
	}

	for (const auto& binding : bindings)
	{
		switch (binding.Trigger)
		{
		case InputTriggerType::Pressed:
			if (raw.Pressed)
			{
				EmitActionEvent(binding, InputPhase::Started, raw.Value, raw.User);
				if (State) State->SetActionState(binding.Action, true, raw.Value);
			}
			break;

		case InputTriggerType::Released:
			if (!raw.Pressed)
			{
				EmitActionEvent(binding, InputPhase::Started, raw.Value, raw.User);
			}
			break;

		case InputTriggerType::Held:
			if (raw.Pressed)
			{
				EmitActionEvent(binding, InputPhase::Started, raw.Value, raw.User);
				ActiveHelds.push_back({
					binding.Action,
					binding.Context,
					raw.User,
					raw.Control,
					raw.Device
				});
				if (State) State->SetActionState(binding.Action, true, raw.Value);
			}
			else
			{
				for (auto it = ActiveHelds.begin(); it != ActiveHelds.end(); ++it)
				{
					if (it->Control == raw.Control
						&& it->Device == raw.Device
						&& it->Action == binding.Action)
					{
						EmitActionEvent(binding, InputPhase::Canceled, 0.0f, raw.User);
						if (State) State->SetActionState(binding.Action, false, 0.0f);
						ActiveHelds.erase(it);
						break;
					}
				}
			}
			break;
		}
	}
}

void InputMappingSystem::EmitActionEvent(
	const CompiledBinding& binding, InputPhase phase,
	float value, InputUserId user)
{
	ActionQueue.GetBuffer().Emplace(
		binding.Action,
		phase,
		value,
		user,
		binding.Context
	);
}

void InputMappingSystem::EmitHeldPerformed()
{
	for (const auto& held : ActiveHelds)
	{
		ActionQueue.GetBuffer().Emplace(
			held.Action,
			InputPhase::Performed,
			1.0f,
			held.User,
			held.Context
		);
	}
}
