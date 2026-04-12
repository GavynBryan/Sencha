#include <input/InputBindingCompiler.h>
#include <input/InputBindingService.h>
#include <input/InputEventQueueService.h>
#include <input/InputMappingSystem.h>
#include <input/InputStateService.h>
#include <input/RawInputBufferService.h>
#include <json/JsonParser.h>
#include <logging/ConsoleLogSink.h>
#include <logging/LoggingProvider.h>
#include <sdl/SdlInputControlResolver.h>
#include <sdl/SdlInputIngestSystem.h>
#include <sdl/SdlWindow.h>
#include <system/ISystem.h>
#include <system/SystemHost.h>

#include <SDL3/SDL.h>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
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

std::string_view ToString(InputPhase phase)
{
	switch (phase)
	{
	case InputPhase::Started:
		return "Started";
	case InputPhase::Performed:
		return "Performed";
	case InputPhase::Canceled:
		return "Canceled";
	default:
		return "Unknown";
	}
}

bool SDLCALL WatchQuitEvents(void* userData, SDL_Event* event)
{
	auto* running = static_cast<bool*>(userData);
	if (event->type == SDL_EVENT_QUIT ||
		event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
	{
		*running = false;
	}

	return true;
}

class SenchaInputSystemExample
{
};

class ConsoleInputOutputSystem final : public ISystem
{
public:
	ConsoleInputOutputSystem(
		InputEventQueueService& events,
		InputBindingService& bindings,
		bool& running)
		: Events(events)
		, Bindings(bindings)
		, Running(running)
	{
	}

private:
	void Update() override
	{
		for (const auto& event : Events.GetBuffer().Items())
		{
			auto action = Bindings.GetActionName(event.Action);
			std::cout
				<< "input "
				<< action
				<< " "
				<< ToString(event.Phase)
				<< " value="
				<< event.Value
				<< "\n";

			if (action == "Quit" && event.Phase == InputPhase::Started)
			{
				Running = false;
			}
		}
	}

	InputEventQueueService& Events;
	InputBindingService& Bindings;
	bool& Running;
};

} // anonymous namespace

int main()
{
	LoggingProvider logging;
	logging.AddSink<ConsoleLogSink>();

	auto& logger = logging.GetLogger<SenchaInputSystemExample>();

	auto configText = ReadTextFile(SENCHA_INPUT_CONFIG_PATH);
	if (!configText)
	{
		logger.Error("Failed to read input config: {}", SENCHA_INPUT_CONFIG_PATH);
		return 1;
	}

	JsonParseError parseError;
	auto json = JsonParse(*configText, &parseError);
	if (!json)
	{
		logger.Error(
			"Failed to parse input config at byte {}: {}",
			parseError.Position,
			parseError.Message);
		return 1;
	}

	InputCompileError compileError;
	auto config = DeserializeInputConfig(*json, &compileError);
	if (!config)
	{
		logger.Error("Failed to deserialize input config: {}", compileError.Message);
		return 1;
	}

	SdlInputControlResolver controlResolver;
	auto table = CompileInputBindings(*config, controlResolver, &compileError);
	if (!table)
	{
		logger.Error("Failed to compile input bindings: {}", compileError.Message);
		return 1;
	}

	WindowCreateInfo windowInfo;
	windowInfo.Title = "Sencha Input System";
	windowInfo.Width = 640;
	windowInfo.Height = 360;
	windowInfo.GraphicsApi = WindowGraphicsApi::None;
	windowInfo.Resizable = false;
	windowInfo.Visible = true;

	SdlWindow window(logger, windowInfo);
	if (!window.IsValid())
	{
		return 1;
	}

	RawInputBufferService rawInput;
	InputBindingService bindings;
	InputEventQueueService actionEvents;
	InputStateService state;
	bindings.SetBindings(std::move(*table));

	bool running = true;
	SDL_AddEventWatch(WatchQuitEvents, &running);

	SystemHost systems;
	systems.AddSystem<SdlInputIngestSystem>(
		0,
		logger,
		rawInput);
	systems.AddSystem<InputMappingSystem>(
		1,
		logger,
		rawInput,
		bindings,
		actionEvents,
		&state);
	systems.AddSystem<ConsoleInputOutputSystem>(
		2,
		actionEvents,
		bindings,
		running);

	systems.Init();

	logger.Info("Focus the window. Space=Jump, A=MoveLeft, Left Mouse=Fire, Escape=Quit.");

	while (running)
	{
		systems.Update();
		SDL_Delay(16);
	}

	systems.Shutdown();
	SDL_RemoveEventWatch(WatchQuitEvents, &running);

	return 0;
}
