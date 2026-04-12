#include <input/InputBindingCompiler.h>
#include <unordered_map>

// --- Control name registry ---------------------------------------------------
// Maps authored control names to numeric codes at load time only.
// Keyboard codes correspond to SDL3 scancode values — the SdlInputIngestSystem
// writes SDL scancodes directly as control codes, so the numeric values match.

namespace
{

struct ControlEntry
{
	const char* Name;
	uint16_t Code;
};

static constexpr ControlEntry KeyboardControls[] = {
	// Letters (SDL_SCANCODE_A = 4 through SDL_SCANCODE_Z = 29)
	{"A", 4}, {"B", 5}, {"C", 6}, {"D", 7}, {"E", 8}, {"F", 9},
	{"G", 10}, {"H", 11}, {"I", 12}, {"J", 13}, {"K", 14}, {"L", 15},
	{"M", 16}, {"N", 17}, {"O", 18}, {"P", 19}, {"Q", 20}, {"R", 21},
	{"S", 22}, {"T", 23}, {"U", 24}, {"V", 25}, {"W", 26}, {"X", 27},
	{"Y", 28}, {"Z", 29},

	// Digits (SDL_SCANCODE_1 = 30 through SDL_SCANCODE_0 = 39)
	{"1", 30}, {"2", 31}, {"3", 32}, {"4", 33}, {"5", 34},
	{"6", 35}, {"7", 36}, {"8", 37}, {"9", 38}, {"0", 39},

	// Common keys
	{"Return", 40}, {"Enter", 40},
	{"Escape", 41},
	{"Backspace", 42},
	{"Tab", 43},
	{"Space", 44},
	{"Minus", 45},
	{"Equals", 46},
	{"LeftBracket", 47},
	{"RightBracket", 48},
	{"Backslash", 49},
	{"Semicolon", 51},
	{"Apostrophe", 52},
	{"Grave", 53},
	{"Comma", 54},
	{"Period", 55},
	{"Slash", 56},
	{"CapsLock", 57},

	// Function keys (SDL_SCANCODE_F1 = 58 through SDL_SCANCODE_F12 = 69)
	{"F1", 58}, {"F2", 59}, {"F3", 60}, {"F4", 61},
	{"F5", 62}, {"F6", 63}, {"F7", 64}, {"F8", 65},
	{"F9", 66}, {"F10", 67}, {"F11", 68}, {"F12", 69},

	// Navigation
	{"Insert", 73}, {"Home", 74}, {"PageUp", 75},
	{"Delete", 76}, {"End", 77}, {"PageDown", 78},

	// Arrows
	{"Right", 79}, {"Left", 80}, {"Down", 81}, {"Up", 82},

	// Modifiers
	{"LCtrl", 224}, {"LShift", 225}, {"LAlt", 226},
	{"RCtrl", 228}, {"RShift", 229}, {"RAlt", 230},
};

static constexpr ControlEntry MouseControls[] = {
	{"Left", 1}, {"Middle", 2}, {"Right", 3},
	{"X1", 4}, {"X2", 5},
	{"WheelUp", 6}, {"WheelDown", 7},
};

std::optional<uint16_t> FindControlCode(
	const ControlEntry* entries, std::size_t count, std::string_view name)
{
	for (std::size_t i = 0; i < count; ++i)
	{
		if (name == entries[i].Name) return entries[i].Code;
	}
	return std::nullopt;
}

std::optional<InputDeviceType> ParseDeviceType(std::string_view name)
{
	if (name == "Keyboard")   return InputDeviceType::Keyboard;
	if (name == "Mouse")      return InputDeviceType::Mouse;
	if (name == "Controller") return InputDeviceType::Controller;
	return std::nullopt;
}

std::optional<InputTriggerType> ParseTriggerType(std::string_view name)
{
	if (name == "Pressed")  return InputTriggerType::Pressed;
	if (name == "Released") return InputTriggerType::Released;
	if (name == "Held")     return InputTriggerType::Held;
	return std::nullopt;
}

} // anonymous namespace

// --- Deserialization ---------------------------------------------------------

std::optional<InputConfigData> DeserializeInputConfig(
	const JsonValue& root, InputCompileError* error)
{
	if (!root.IsObject())
	{
		if (error) error->Message = "Root must be a JSON object";
		return std::nullopt;
	}

	InputConfigData config;

	// Actions
	const auto* actions = root.Find("actions");
	if (!actions || !actions->IsArray())
	{
		if (error) error->Message = "Missing or invalid 'actions' array";
		return std::nullopt;
	}

	for (const auto& item : actions->AsArray())
	{
		if (!item.IsObject())
		{
			if (error) error->Message = "Action entry must be an object";
			return std::nullopt;
		}
		const auto* name = item.Find("name");
		if (!name || !name->IsString())
		{
			if (error) error->Message = "Action missing 'name' string";
			return std::nullopt;
		}
		config.Actions.push_back({name->AsString()});
	}

	// Bindings
	const auto* bindings = root.Find("bindings");
	if (!bindings || !bindings->IsArray())
	{
		if (error) error->Message = "Missing or invalid 'bindings' array";
		return std::nullopt;
	}

	for (const auto& item : bindings->AsArray())
	{
		if (!item.IsObject())
		{
			if (error) error->Message = "Binding entry must be an object";
			return std::nullopt;
		}

		auto getStr = [&](const char* key) -> std::optional<std::string> {
			const auto* v = item.Find(key);
			if (!v || !v->IsString()) return std::nullopt;
			return v->AsString();
		};

		auto action  = getStr("action");
		auto device  = getStr("device");
		auto control = getStr("control");
		auto trigger = getStr("trigger");

		if (!action || !device || !control || !trigger)
		{
			if (error) error->Message = "Binding missing required field (action, device, control, trigger)";
			return std::nullopt;
		}

		config.Bindings.push_back({*action, *device, *control, *trigger});
	}

	// Contexts (optional)
	const auto* contexts = root.Find("contexts");
	if (contexts && contexts->IsArray())
	{
		for (const auto& item : contexts->AsArray())
		{
			if (!item.IsObject()) continue;
			InputContextConfig cc;
			const auto* name = item.Find("name");
			if (name && name->IsString()) cc.Name = name->AsString();
			const auto* acts = item.Find("actions");
			if (acts && acts->IsArray())
			{
				for (const auto& a : acts->AsArray())
				{
					if (a.IsString()) cc.Actions.push_back(a.AsString());
				}
			}
			config.Contexts.push_back(std::move(cc));
		}
	}

	return config;
}

// --- Compilation -------------------------------------------------------------

std::optional<InputBindingTable> CompileInputBindings(
	const InputConfigData& config, InputCompileError* error)
{
	InputBindingTable table;

	// Build action name -> ID mapping (IDs start at 1)
	std::unordered_map<std::string, InputActionId> actionMap;
	for (const auto& action : config.Actions)
	{
		InputActionId id{static_cast<uint16_t>(table.ActionNames.size() + 1)};
		actionMap[action.Name] = id;
		table.ActionNames.push_back(action.Name);
	}
	table.ActionCount = static_cast<uint16_t>(table.ActionNames.size());

	// Temporary per-control binding lists (built then flattened)
	std::unordered_map<uint16_t, std::vector<CompiledBinding>> keyboardTemp;
	std::unordered_map<uint16_t, std::vector<CompiledBinding>> mouseTemp;

	for (const auto& bc : config.Bindings)
	{
		auto actionIt = actionMap.find(bc.Action);
		if (actionIt == actionMap.end())
		{
			if (error) error->Message = "Unknown action: " + bc.Action;
			return std::nullopt;
		}

		auto device = ParseDeviceType(bc.Device);
		if (!device)
		{
			if (error) error->Message = "Unknown device: " + bc.Device;
			return std::nullopt;
		}

		auto trigger = ParseTriggerType(bc.Trigger);
		if (!trigger)
		{
			if (error) error->Message = "Unknown trigger: " + bc.Trigger;
			return std::nullopt;
		}

		std::optional<uint16_t> controlCode;
		switch (*device)
		{
		case InputDeviceType::Keyboard:
			controlCode = FindControlCode(
				KeyboardControls, std::size(KeyboardControls), bc.Control);
			break;
		case InputDeviceType::Mouse:
			controlCode = FindControlCode(
				MouseControls, std::size(MouseControls), bc.Control);
			break;
		default:
			if (error) error->Message = "Unsupported device for first pass: " + bc.Device;
			return std::nullopt;
		}

		if (!controlCode)
		{
			if (error) error->Message = "Unknown control '" + bc.Control
				+ "' for device " + bc.Device;
			return std::nullopt;
		}

		CompiledBinding compiled{
			actionIt->second,
			InputContextId{},   // default context for first pass
			InputUserId{},      // default user for first pass
			*trigger
		};

		switch (*device)
		{
		case InputDeviceType::Keyboard:
			keyboardTemp[*controlCode].push_back(compiled);
			break;
		case InputDeviceType::Mouse:
			mouseTemp[*controlCode].push_back(compiled);
			break;
		default:
			break;
		}
	}

	// Flatten keyboard bindings into packed array
	for (auto& [control, bindings] : keyboardTemp)
	{
		if (control >= MaxKeyboardControls) continue;
		table.KeyboardSlots[control].Start =
			static_cast<uint16_t>(table.KeyboardBindings.size());
		table.KeyboardSlots[control].Count =
			static_cast<uint16_t>(bindings.size());
		for (auto& b : bindings)
		{
			table.KeyboardBindings.push_back(b);
		}
	}

	// Flatten mouse bindings into packed array
	for (auto& [control, bindings] : mouseTemp)
	{
		if (control >= MaxMouseButtons) continue;
		table.MouseButtonSlots[control].Start =
			static_cast<uint16_t>(table.MouseButtonBindings.size());
		table.MouseButtonSlots[control].Count =
			static_cast<uint16_t>(bindings.size());
		for (auto& b : bindings)
		{
			table.MouseButtonBindings.push_back(b);
		}
	}

	return table;
}
