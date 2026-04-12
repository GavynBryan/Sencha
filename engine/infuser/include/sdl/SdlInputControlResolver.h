#pragma once

#include <input/InputBindingCompiler.h>

//=============================================================================
// SdlInputControlResolver
//
// Resolves authored input control names to the same engine control codes that
// SdlInputIngestSystem writes into RawInputEvent.
//=============================================================================
class SdlInputControlResolver final : public IInputControlResolver
{
public:
	[[nodiscard]] std::optional<uint16_t> ResolveControl(
		InputDeviceType device,
		std::string_view control) const override;
};
