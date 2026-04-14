#include "QuadTreeDemoInput.h"

#include "QuadTreeDemoGame.h"

#include <core/json/JsonParser.h>
#include <core/logging/Logger.h>
#include <input/InputActionRegistry.h>
#include <input/InputBindingCompiler.h>
#include <input/InputBindingTable.h>
#include <input/InputTypes.h>
#include <input/SdlInputControlResolver.h>
#include <input/SdlInputSystem.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace
{
	std::optional<std::string> ReadTextFile(const char* path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file) return std::nullopt;

		std::ostringstream buffer;
		buffer << file.rdbuf();
		return buffer.str();
	}
}

std::optional<InputBindingTable> LoadQuadTreeDemoInputBindings(Logger& logger)
{
	auto configText = ReadTextFile(SENCHA_QUADTREE_INPUT_CONFIG_PATH);
	if (!configText)
	{
		logger.Error("Failed to read input config: {}", SENCHA_QUADTREE_INPUT_CONFIG_PATH);
		return std::nullopt;
	}

	JsonParseError parseError;
	auto json = JsonParse(*configText, &parseError);
	if (!json)
	{
		logger.Error(
			"Failed to parse input config at byte {}: {}",
			parseError.Position,
			parseError.Message);
		return std::nullopt;
	}

	InputCompileError compileError;
	auto config = DeserializeInputConfig(*json, &compileError);
	if (!config)
	{
		logger.Error("Failed to deserialize input config: {}", compileError.Message);
		return std::nullopt;
	}

	SdlInputControlResolver controlResolver;
	InputActionRegistry actionRegistry{QuadTreeDemoActions::Names};
	auto table = CompileInputBindings(*config, actionRegistry, controlResolver, &compileError);
	if (!table)
	{
		logger.Error("Failed to compile input bindings: {}", compileError.Message);
		return std::nullopt;
	}

	return std::move(*table);
}

void InjectQuadTreeDemoInputEvent(SdlInputSystem& input, const SDL_Event& event)
{
	switch (event.type)
	{
	case SDL_EVENT_KEY_DOWN:
		if (!event.key.repeat)
		{
			input.GetRawInput().Emplace(
				InputDeviceType::Keyboard, true,
				static_cast<uint16_t>(event.key.scancode), 1.0f, InputUserId{});
		}
		break;

	case SDL_EVENT_KEY_UP:
		input.GetRawInput().Emplace(
			InputDeviceType::Keyboard, false,
			static_cast<uint16_t>(event.key.scancode), 0.0f, InputUserId{});
		break;

	default:
		break;
	}
}
