#pragma once

#include <platform/InputTypes.h>
#include <optional>
#include <string_view>

//=============================================================================
// SdlInputControlResolver
//
// Resolves authored input control names to the same engine control codes that
// SdlInputSystem writes into RawInputEvent.
//=============================================================================
class SdlInputControlResolver
{
public:
	[[nodiscard]] std::optional<uint16_t> ResolveControl(
		InputDeviceType device,
		std::string_view control) const;
};
