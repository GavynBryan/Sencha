#pragma once

#include <system/ISystem.h>
#include <input/InputTypes.h>
#include <logging/Logger.h>
#include <vector>

class RawInputBufferService;
class InputBindingService;
class InputEventQueueService;
class InputStateService;
struct CompiledBinding;

//=============================================================================
// InputMappingSystem
//
// Core of the input runtime. Consumes raw input events and compiled
// bindings, emits semantic action events.
//
// Per frame:
//   1. Clears the action event queue
//   2. For each raw event, looks up compiled bindings by device + control
//   3. Emits InputActionEvents based on trigger type and press state
//   4. Tracks held bindings and emits Performed each frame
//   5. Optionally updates InputStateService
//
// Execution order: must run after backend input ingest, before gameplay.
//=============================================================================
class InputMappingSystem : public ISystem
{
public:
	InputMappingSystem(
		Logger& logger,
		RawInputBufferService& rawInput,
		InputBindingService& bindings,
		InputEventQueueService& actionQueue,
		InputStateService* stateService = nullptr);

private:
	void Update() override;

	void ProcessRawEvent(const RawInputEvent& raw);
	void EmitActionEvent(const CompiledBinding& binding, InputPhase phase,
		float value, InputUserId user);
	void EmitHeldPerformed();

	struct ActiveHeld
	{
		InputActionId Action;
		InputContextId Context;
		InputUserId User;
		uint16_t Control;
		InputDeviceType Device;
	};

	Logger& Log;
	RawInputBufferService& RawInput;
	InputBindingService& Bindings;
	InputEventQueueService& ActionQueue;
	InputStateService* State;

	std::vector<ActiveHeld> ActiveHelds;
};
