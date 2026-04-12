#pragma once

#include <system/ISystem.h>
#include <logging/Logger.h>

class RawInputBufferService;

//=============================================================================
// SdlInputIngestSystem
//
// Backend-facing input layer. Translates SDL3 input events into compact
// engine-owned RawInputEvents. Knows only device facts — no gameplay
// concepts like Jump or Pause.
//
// Per frame:
//   1. Clears the raw input buffer
//   2. Polls all pending SDL events
//   3. Writes keyboard, mouse button, and mouse wheel events
//
// Execution order: must run before InputMappingSystem.
//=============================================================================
class SdlInputIngestSystem : public ISystem
{
public:
	SdlInputIngestSystem(Logger& logger, RawInputBufferService& rawInput);

private:
	void Update() override;

	Logger& Log;
	RawInputBufferService& RawInput;
};
