#pragma once

#include <service/IService.h>
#include <event/EventBuffer.h>
#include <input/InputTypes.h>

//=============================================================================
// RawInputBufferService
//
// Owns raw backend/platform input events for the current frame.
// Written by SdlInputIngestSystem, consumed by InputMappingSystem.
//
// Lifecycle per frame:
//   1. SdlInputIngestSystem clears and fills the buffer
//   2. InputMappingSystem reads the buffer
//   3. Buffer persists until next frame's clear
//=============================================================================
class RawInputBufferService : public IService
{
public:
	EventBuffer<RawInputEvent>& GetBuffer() { return Buffer; }
	const EventBuffer<RawInputEvent>& GetBuffer() const { return Buffer; }

private:
	EventBuffer<RawInputEvent> Buffer;
};
