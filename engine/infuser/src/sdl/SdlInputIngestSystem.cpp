#include <sdl/SdlInputIngestSystem.h>
#include <input/RawInputBufferService.h>
#include <input/InputTypes.h>
#include <SDL3/SDL.h>
#include <optional>

namespace
{

std::optional<uint16_t> ToMouseControlCode(uint8_t button)
{
	switch (button)
	{
	case SDL_BUTTON_LEFT:
		return MouseControl::Left;
	case SDL_BUTTON_MIDDLE:
		return MouseControl::Middle;
	case SDL_BUTTON_RIGHT:
		return MouseControl::Right;
	case SDL_BUTTON_X1:
		return MouseControl::X1;
	case SDL_BUTTON_X2:
		return MouseControl::X2;
	default:
		return std::nullopt;
	}
}

} // anonymous namespace

SdlInputIngestSystem::SdlInputIngestSystem(
	LoggingProvider& logging, RawInputBufferService& rawInput)
	: Log(logging.GetLogger<SdlInputIngestSystem>())
	, RawInput(rawInput)
{
}

void SdlInputIngestSystem::Update()
{
	auto& buffer = RawInput.GetBuffer();
	buffer.Clear();

	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_EVENT_KEY_DOWN:
			if (!event.key.repeat)
			{
				buffer.Emplace(
					InputDeviceType::Keyboard,
					true,
					static_cast<uint16_t>(event.key.scancode),
					1.0f,
					InputUserId{}
				);
			}
			break;

		case SDL_EVENT_KEY_UP:
			buffer.Emplace(
				InputDeviceType::Keyboard,
				false,
				static_cast<uint16_t>(event.key.scancode),
				0.0f,
				InputUserId{}
			);
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (auto control = ToMouseControlCode(event.button.button))
			{
				buffer.Emplace(
					InputDeviceType::Mouse,
					true,
					*control,
					1.0f,
					InputUserId{}
				);
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_UP:
			if (auto control = ToMouseControlCode(event.button.button))
			{
				buffer.Emplace(
					InputDeviceType::Mouse,
					false,
					*control,
					0.0f,
					InputUserId{}
				);
			}
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			if (event.wheel.y > 0)
			{
				buffer.Emplace(
					InputDeviceType::Mouse,
					true,
					MouseControl::WheelUp,
					event.wheel.y,
					InputUserId{}
				);
			}
			else if (event.wheel.y < 0)
			{
				buffer.Emplace(
					InputDeviceType::Mouse,
					true,
					MouseControl::WheelDown,
					-event.wheel.y,
					InputUserId{}
				);
			}
			break;
		}
	}
}
