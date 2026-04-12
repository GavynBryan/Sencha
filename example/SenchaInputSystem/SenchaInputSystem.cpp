#include <input/InputActionRegistry.h>
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

namespace ExampleActions
{
	static constexpr std::string_view Names[] = {"Jump", "MoveLeft", "Shoot", "Quit"};
	enum Action : uint16_t { Jump, MoveLeft, Shoot, Quit, Count };
}

class ConsoleInputOutputSystem final : public ISystem
{
public:
	ConsoleInputOutputSystem(
		InputEventQueueService& events,
		InputActionId quitAction,
		bool& running)
		: Events(events)
		, QuitAction(quitAction)
		, Running(running)
	{
	}

private:
	void Update() override
	{
		for (const auto& event : Events.GetBuffer().Items())
		{
			std::cout
				<< "input "
				<< event.Action.Value
				<< " "
				<< ToString(event.Phase)
				<< " value="
				<< event.Value
				<< "\n";

			if (event.Action == QuitAction && event.Phase == InputPhase::Started)
			{
				Running = false;
			}
		}
	}

	InputEventQueueService& Events;
	InputActionId QuitAction;
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
	InputActionRegistry actionRegistry{ExampleActions::Names};
	auto table = CompileInputBindings(
		*config,
		actionRegistry,
		controlResolver,
		&compileError);
	if (!table)
	{
		logger.Error("Failed to compile input bindings: {}", compileError.Message);
		return 1;
	}

	for (uint16_t i = 0; i < table->ActionNames.size(); ++i)
	{
		logger.Info("Action {} = {}", i, table->ActionNames[i]);
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
	InputStateService state{ExampleActions::Count};
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
		ExampleActions::Quit,
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
