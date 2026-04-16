#include <input/SdlInputSystem.h>
#include <input/InputBindingService.h>
#include <input/InputBindingTable.h>
#include <SDL3/SDL.h>
#include <optional>

namespace
{

std::optional<uint16_t> ToMouseControlCode(uint8_t button)
{
	switch (button)
	{
	case SDL_BUTTON_LEFT:   return MouseControl::Left;
	case SDL_BUTTON_MIDDLE: return MouseControl::Middle;
	case SDL_BUTTON_RIGHT:  return MouseControl::Right;
	case SDL_BUTTON_X1:     return MouseControl::X1;
	case SDL_BUTTON_X2:     return MouseControl::X2;
	default:                return std::nullopt;
	}
}

} // anonymous namespace

SdlInputSystem::SdlInputSystem(LoggingProvider& logging, InputBindingService& bindings)
	: Log(logging.GetLogger<SdlInputSystem>())
	, Bindings(bindings)
{
}

void SdlInputSystem::Update(float /*dt*/)
{
	ActionEvents.Clear();
	IngestFromSdl();
	MapRawToActions();
	RawBuffer.Clear();
}

void SdlInputSystem::IngestFromSdl()
{
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_EVENT_KEY_DOWN:
			if (!event.key.repeat)
			{
				RawBuffer.Emplace(
					InputDeviceType::Keyboard, true,
					static_cast<uint16_t>(event.key.scancode), 1.0f, InputUserId{});
			}
			break;

		case SDL_EVENT_KEY_UP:
			RawBuffer.Emplace(
				InputDeviceType::Keyboard, false,
				static_cast<uint16_t>(event.key.scancode), 0.0f, InputUserId{});
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (auto control = ToMouseControlCode(event.button.button))
				RawBuffer.Emplace(InputDeviceType::Mouse, true, *control, 1.0f, InputUserId{});
			break;

		case SDL_EVENT_MOUSE_BUTTON_UP:
			if (auto control = ToMouseControlCode(event.button.button))
				RawBuffer.Emplace(InputDeviceType::Mouse, false, *control, 0.0f, InputUserId{});
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			if (event.wheel.y > 0)
				RawBuffer.Emplace(InputDeviceType::Mouse, true,
					MouseControl::WheelUp, event.wheel.y, InputUserId{});
			else if (event.wheel.y < 0)
				RawBuffer.Emplace(InputDeviceType::Mouse, true,
					MouseControl::WheelDown, -event.wheel.y, InputUserId{});
			break;
		}
	}
}

void SdlInputSystem::MapRawToActions()
{
	for (const auto& raw : RawBuffer.Items())
		ProcessRawEvent(raw);
	EmitHeldPerformed();
}

void SdlInputSystem::ProcessRawEvent(const RawInputEvent& raw)
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
				EmitActionEvent(binding, InputPhase::Started, raw.Value, raw.User);
			break;

		case InputTriggerType::Released:
			if (!raw.Pressed)
				EmitActionEvent(binding, InputPhase::Started, raw.Value, raw.User);
			break;

		case InputTriggerType::Held:
			if (raw.Pressed)
			{
				EmitActionEvent(binding, InputPhase::Started, raw.Value, raw.User);
				ActiveHelds.push_back({
					binding.Action, binding.Context, raw.User, raw.Control, raw.Device
				});
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
						ActiveHelds.erase(it);
						break;
					}
				}
			}
			break;
		}
	}
}

void SdlInputSystem::EmitActionEvent(
	const CompiledBinding& binding, InputPhase phase, float value, InputUserId user)
{
	ActionEvents.Emplace(binding.Action, phase, value, user, binding.Context);
}

void SdlInputSystem::EmitHeldPerformed()
{
	for (const auto& held : ActiveHelds)
	{
		ActionEvents.Emplace(
			held.Action, InputPhase::Performed, 1.0f, held.User, held.Context);
	}
}
