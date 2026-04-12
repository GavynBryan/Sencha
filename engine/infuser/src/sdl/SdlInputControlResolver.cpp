#include <sdl/SdlInputControlResolver.h>
#include <input/InputTypes.h>
#include <SDL3/SDL.h>
#include <string>

namespace
{

struct KeyboardAlias
{
	const char* Name;
	SDL_Scancode Code;
};

static constexpr KeyboardAlias KeyboardAliases[] = {
	{"A", SDL_SCANCODE_A}, {"B", SDL_SCANCODE_B},
	{"C", SDL_SCANCODE_C}, {"D", SDL_SCANCODE_D},
	{"E", SDL_SCANCODE_E}, {"F", SDL_SCANCODE_F},
	{"G", SDL_SCANCODE_G}, {"H", SDL_SCANCODE_H},
	{"I", SDL_SCANCODE_I}, {"J", SDL_SCANCODE_J},
	{"K", SDL_SCANCODE_K}, {"L", SDL_SCANCODE_L},
	{"M", SDL_SCANCODE_M}, {"N", SDL_SCANCODE_N},
	{"O", SDL_SCANCODE_O}, {"P", SDL_SCANCODE_P},
	{"Q", SDL_SCANCODE_Q}, {"R", SDL_SCANCODE_R},
	{"S", SDL_SCANCODE_S}, {"T", SDL_SCANCODE_T},
	{"U", SDL_SCANCODE_U}, {"V", SDL_SCANCODE_V},
	{"W", SDL_SCANCODE_W}, {"X", SDL_SCANCODE_X},
	{"Y", SDL_SCANCODE_Y}, {"Z", SDL_SCANCODE_Z},

	{"1", SDL_SCANCODE_1}, {"2", SDL_SCANCODE_2},
	{"3", SDL_SCANCODE_3}, {"4", SDL_SCANCODE_4},
	{"5", SDL_SCANCODE_5}, {"6", SDL_SCANCODE_6},
	{"7", SDL_SCANCODE_7}, {"8", SDL_SCANCODE_8},
	{"9", SDL_SCANCODE_9}, {"0", SDL_SCANCODE_0},

	{"Return", SDL_SCANCODE_RETURN}, {"Enter", SDL_SCANCODE_RETURN},
	{"Escape", SDL_SCANCODE_ESCAPE},
	{"Backspace", SDL_SCANCODE_BACKSPACE},
	{"Tab", SDL_SCANCODE_TAB},
	{"Space", SDL_SCANCODE_SPACE},
	{"Minus", SDL_SCANCODE_MINUS},
	{"Equals", SDL_SCANCODE_EQUALS},
	{"LeftBracket", SDL_SCANCODE_LEFTBRACKET},
	{"RightBracket", SDL_SCANCODE_RIGHTBRACKET},
	{"Backslash", SDL_SCANCODE_BACKSLASH},
	{"Semicolon", SDL_SCANCODE_SEMICOLON},
	{"Apostrophe", SDL_SCANCODE_APOSTROPHE},
	{"Grave", SDL_SCANCODE_GRAVE},
	{"Comma", SDL_SCANCODE_COMMA},
	{"Period", SDL_SCANCODE_PERIOD},
	{"Slash", SDL_SCANCODE_SLASH},
	{"CapsLock", SDL_SCANCODE_CAPSLOCK},

	{"F1", SDL_SCANCODE_F1}, {"F2", SDL_SCANCODE_F2},
	{"F3", SDL_SCANCODE_F3}, {"F4", SDL_SCANCODE_F4},
	{"F5", SDL_SCANCODE_F5}, {"F6", SDL_SCANCODE_F6},
	{"F7", SDL_SCANCODE_F7}, {"F8", SDL_SCANCODE_F8},
	{"F9", SDL_SCANCODE_F9}, {"F10", SDL_SCANCODE_F10},
	{"F11", SDL_SCANCODE_F11}, {"F12", SDL_SCANCODE_F12},

	{"Insert", SDL_SCANCODE_INSERT},
	{"Home", SDL_SCANCODE_HOME},
	{"PageUp", SDL_SCANCODE_PAGEUP},
	{"Delete", SDL_SCANCODE_DELETE},
	{"End", SDL_SCANCODE_END},
	{"PageDown", SDL_SCANCODE_PAGEDOWN},

	{"Right", SDL_SCANCODE_RIGHT},
	{"Left", SDL_SCANCODE_LEFT},
	{"Down", SDL_SCANCODE_DOWN},
	{"Up", SDL_SCANCODE_UP},

	{"LCtrl", SDL_SCANCODE_LCTRL},
	{"LShift", SDL_SCANCODE_LSHIFT},
	{"LAlt", SDL_SCANCODE_LALT},
	{"RCtrl", SDL_SCANCODE_RCTRL},
	{"RShift", SDL_SCANCODE_RSHIFT},
	{"RAlt", SDL_SCANCODE_RALT},
};

struct MouseAlias
{
	const char* Name;
	uint16_t Code;
};

static constexpr MouseAlias MouseAliases[] = {
	{"Left", MouseControl::Left},
	{"Middle", MouseControl::Middle},
	{"Right", MouseControl::Right},
	{"X1", MouseControl::X1},
	{"X2", MouseControl::X2},
	{"WheelUp", MouseControl::WheelUp},
	{"WheelDown", MouseControl::WheelDown},
};

std::optional<uint16_t> FindKeyboardAlias(std::string_view name)
{
	for (const auto& alias : KeyboardAliases)
	{
		if (name == alias.Name) return static_cast<uint16_t>(alias.Code);
	}

	return std::nullopt;
}

std::optional<uint16_t> ResolveKeyboardControl(std::string_view control)
{
	if (auto alias = FindKeyboardAlias(control))
	{
		return alias;
	}

	std::string sdlName{control};
	SDL_Scancode scancode = SDL_GetScancodeFromName(sdlName.c_str());
	if (scancode == SDL_SCANCODE_UNKNOWN)
	{
		return std::nullopt;
	}

	return static_cast<uint16_t>(scancode);
}

std::optional<uint16_t> ResolveMouseControl(std::string_view control)
{
	for (const auto& alias : MouseAliases)
	{
		if (control == alias.Name) return alias.Code;
	}

	return std::nullopt;
}

} // anonymous namespace

std::optional<uint16_t> SdlInputControlResolver::ResolveControl(
	InputDeviceType device,
	std::string_view control) const
{
	switch (device)
	{
	case InputDeviceType::Keyboard:
		return ResolveKeyboardControl(control);
	case InputDeviceType::Mouse:
		return ResolveMouseControl(control);
	default:
		return std::nullopt;
	}
}
