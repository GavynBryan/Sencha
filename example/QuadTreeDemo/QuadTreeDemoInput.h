#pragma once

#include <optional>

union SDL_Event;
class Logger;
class SdlInputSystem;
struct InputBindingTable;

std::optional<InputBindingTable> LoadQuadTreeDemoInputBindings(Logger& logger);
void InjectQuadTreeDemoInputEvent(SdlInputSystem& input, const SDL_Event& event);
