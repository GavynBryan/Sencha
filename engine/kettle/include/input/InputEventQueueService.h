#pragma once

#include <service/IService.h>
#include <event/EventBuffer.h>
#include <input/InputTypes.h>

//=============================================================================
// InputEventQueueService
//
// Owns semantic input action events for the current frame.
// Written by InputMappingSystem, consumed by gameplay systems.
//
// This is the primary consumption point for event-driven input.
// Gameplay systems iterate Items() and react to action events,
// rather than polling global input state.
//
// Lifecycle per frame:
//   1. InputMappingSystem clears and fills the buffer
//   2. Gameplay systems read the buffer
//   3. Buffer persists until next frame's clear
//=============================================================================
class InputEventQueueService : public IService
{
public:
	EventBuffer<InputActionEvent>& GetBuffer() { return Buffer; }
	const EventBuffer<InputActionEvent>& GetBuffer() const { return Buffer; }

private:
	EventBuffer<InputActionEvent> Buffer;
};
