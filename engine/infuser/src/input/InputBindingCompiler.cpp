#include <input/InputBindingCompiler.h>
#include <unordered_map>

namespace
{

bool PopulateActionNames(
	const IInputActionResolver& actionResolver,
	InputBindingTable& table,
	InputCompileError* error)
{
	auto names = actionResolver.GetActionNames();
	table.ActionCount = actionResolver.GetActionCount();
	table.ActionNames.resize(table.ActionCount);

	for (uint16_t i = 0; i < table.ActionCount; ++i)
	{
		if (names[i].empty())
		{
			if (error) error->Message = "Action registry contains an empty action name";
			return false;
		}
		table.ActionNames[i] = names[i];
	}

	return true;
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

	const auto* actions = root.Find("actions");
	if (actions)
	{
		if (!actions->IsArray())
		{
			if (error) error->Message = "Invalid 'actions' array";
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
	const InputConfigData& config,
	const IInputActionResolver& actionResolver,
	const SdlInputControlResolver& controlResolver,
	InputCompileError* error)
{
	InputBindingTable table;

	if (!PopulateActionNames(actionResolver, table, error))
	{
		return std::nullopt;
	}

	for (const auto& action : config.Actions)
	{
		if (!actionResolver.ResolveAction(action.Name))
		{
			if (error) error->Message = "Unknown action in diagnostics: " + action.Name;
			return std::nullopt;
		}
	}

	// Temporary per-control binding lists (built then flattened)
	std::unordered_map<uint16_t, std::vector<CompiledBinding>> keyboardTemp;
	std::unordered_map<uint16_t, std::vector<CompiledBinding>> mouseTemp;

	for (const auto& bc : config.Bindings)
	{
		auto action = actionResolver.ResolveAction(bc.Action);
		if (!action)
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

		switch (*device)
		{
		case InputDeviceType::Keyboard:
		case InputDeviceType::Mouse:
			break;
		default:
			if (error) error->Message = "Unsupported device for binding table: " + bc.Device;
			return std::nullopt;
		}

		auto controlCode = controlResolver.ResolveControl(*device, bc.Control);
		if (!controlCode)
		{
			if (error) error->Message = "Unknown control '" + bc.Control
				+ "' for device " + bc.Device;
			return std::nullopt;
		}

		CompiledBinding compiled{
			*action,
			InputContextId{},
			InputUserId{},
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
		table.KeyboardBindings.insert(
			table.KeyboardBindings.end(), bindings.begin(), bindings.end());
	}

	// Flatten mouse bindings into packed array
	for (auto& [control, bindings] : mouseTemp)
	{
		if (control >= MaxMouseButtons) continue;
		table.MouseButtonSlots[control].Start =
			static_cast<uint16_t>(table.MouseButtonBindings.size());
		table.MouseButtonSlots[control].Count =
			static_cast<uint16_t>(bindings.size());
		table.MouseButtonBindings.insert(
			table.MouseButtonBindings.end(), bindings.begin(), bindings.end());
	}

	return table;
}
