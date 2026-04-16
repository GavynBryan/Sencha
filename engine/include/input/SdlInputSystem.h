#pragma once

#include <core/event/EventBuffer.h>
#include <input/InputTypes.h>
#include <core/logging/LoggingProvider.h>
#include <vector>

class InputBindingService;
struct CompiledBinding;

//=============================================================================
// SdlInputSystem
//
// Unified input system: ingests SDL3 events and maps them to semantic
// action events within a single Update(). Owns both the raw event buffer
// and the action event buffer â€” no intermediate services required.
//
// Per frame:
//   1. Clears the action event buffer
//   2. Polls all pending SDL events into the raw buffer
//   3. Maps raw events -> action events via the compiled binding table
//   4. Emits Performed events for held bindings
//   5. Clears the raw buffer
//
// Test seam: inject raw events into GetRawInput() before calling Update().
// SDL_PollEvent will find nothing in a test environment, so injected
// events are the only ones that reach the mapping step.
//=============================================================================
class SdlInputSystem
{
public:
	SdlInputSystem(LoggingProvider& logging, InputBindingService& bindings);

	const EventBuffer<InputActionEvent>& GetEvents() const { return ActionEvents; }
	EventBuffer<RawInputEvent>& GetRawInput() { return RawBuffer; }

	void Update(float dt);

private:
	void IngestFromSdl();
	void MapRawToActions();
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
	InputBindingService& Bindings;
	EventBuffer<RawInputEvent> RawBuffer;
	EventBuffer<InputActionEvent> ActionEvents;
	std::vector<ActiveHeld> ActiveHelds;
};
