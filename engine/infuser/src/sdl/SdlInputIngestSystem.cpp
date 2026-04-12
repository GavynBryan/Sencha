#include <sdl/SdlInputIngestSystem.h>
#include <input/RawInputBufferService.h>
#include <input/InputTypes.h>
#include <SDL3/SDL.h>

SdlInputIngestSystem::SdlInputIngestSystem(
	Logger& logger, RawInputBufferService& rawInput)
	: Log(logger)
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
			buffer.Emplace(
				InputDeviceType::Mouse,
				true,
				static_cast<uint16_t>(event.button.button),
				1.0f,
				InputUserId{}
			);
			break;

		case SDL_EVENT_MOUSE_BUTTON_UP:
			buffer.Emplace(
				InputDeviceType::Mouse,
				false,
				static_cast<uint16_t>(event.button.button),
				0.0f,
				InputUserId{}
			);
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
