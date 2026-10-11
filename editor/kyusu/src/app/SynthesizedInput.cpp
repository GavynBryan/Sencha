#include "app/SynthesizedInput.h"

#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleTypes.h>

#include <SDL3/SDL_keyboard.h>

#include <charconv>
#include <string>
#include <string_view>

namespace
{
bool ParseNumber(std::string_view text, float& out)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc{} && end == text.data() + text.size();
}

bool ParseFrame(std::span<const std::string> args, std::size_t at, std::uint64_t& out)
{
    if (args.size() <= at)
        return true;
    const std::string& text = args[at];
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc{} && end == text.data() + text.size();
}
SDL_Keymod ModifierOf(SDL_Keycode key)
{
    switch (key)
    {
    case SDLK_LSHIFT: return SDL_KMOD_LSHIFT;
    case SDLK_RSHIFT: return SDL_KMOD_RSHIFT;
    case SDLK_LCTRL:  return SDL_KMOD_LCTRL;
    case SDLK_RCTRL:  return SDL_KMOD_RCTRL;
    case SDLK_LALT:   return SDL_KMOD_LALT;
    case SDLK_RALT:   return SDL_KMOD_RALT;
    default:          return SDL_KMOD_NONE;
    }
}
} // namespace

void SynthesizedInput::Register(ConsoleRegistry& registry, std::function<std::uint32_t()> window)
{
    registry.RegisterCommand({
        .Name = "input.key",
        .Owner = "editor",
        .Usage = "input.key <key name> <down|up> [frame]",
        .Help = "Press or release a key on the main window, as the platform would report it. With a frame "
                "number, waits until that frame.",
        .Callback = [this, window](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            const SDL_Keycode key = args.size() >= 2 ? SDL_GetKeyFromName(args[0].c_str()) : SDLK_UNKNOWN;
            std::uint64_t frame = Frame;
            if (key == SDLK_UNKNOWN || (args[1] != "down" && args[1] != "up") || !ParseFrame(args, 2, frame))
            {
                result.Error("usage: input.key <key name> <down|up> [frame]");
                return result;
            }
            SDL_Event event{};
            const bool down = args[1] == "down";
            event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.windowID = window();
            event.key.key = key;
            event.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
            event.key.down = down;
            Queue.push_back({ frame, event });
            return result;
        },
    });
    registry.RegisterCommand({
        .Name = "input.drop",
        .Owner = "editor",
        .Usage = "input.drop <file> [frame]",
        .Help = "Drop a file from outside on the main window where the pointer last moved, as the platform "
                "would report it. With a frame number, waits until that frame.",
        .Callback = [this, window](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            std::uint64_t frame = Frame;
            if (args.empty() || !ParseFrame(args, 1, frame))
            {
                result.Error("usage: input.drop <file> [frame]");
                return result;
            }
            DroppedPaths.push_back(args[0]);
            SDL_Event event{};
            event.type = SDL_EVENT_DROP_FILE;
            event.drop.windowID = window();
            event.drop.data = DroppedPaths.back().c_str();
            Queue.push_back({ frame, event });
            return result;
        },
    });
    registry.RegisterCommand({
        .Name = "input.pointer",
        .Owner = "editor",
        .Usage = "input.pointer <x> <y> [frame]",
        .Help = "Move the pointer to window pixel (x, y) on the main window. With a frame number, waits until "
                "that frame.",
        .Callback = [this, window](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            float x = 0.0f;
            float y = 0.0f;
            std::uint64_t frame = Frame;
            if (args.size() < 2 || !ParseNumber(args[0], x) || !ParseNumber(args[1], y) || !ParseFrame(args, 2, frame))
            {
                result.Error("usage: input.pointer <x> <y> [frame]");
                return result;
            }
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.motion.windowID = window();
            event.motion.x = x;
            event.motion.y = y;
            Queue.push_back({ frame, event });
            return result;
        },
    });
    registry.RegisterCommand({
        .Name = "input.button",
        .Owner = "editor",
        .Usage = "input.button <left|right|middle> <down|up> [frame]",
        .Help = "Press or release a mouse button where the pointer last moved. With a frame number, waits until "
                "that frame.",
        .Callback = [this, window](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            std::uint64_t frame = Frame;
            const Uint8 button = args.empty()           ? 0
                               : args[0] == "left"   ? SDL_BUTTON_LEFT
                               : args[0] == "right"  ? SDL_BUTTON_RIGHT
                               : args[0] == "middle" ? SDL_BUTTON_MIDDLE
                                                     : 0;
            if (button == 0 || args.size() < 2 || (args[1] != "down" && args[1] != "up")
                || !ParseFrame(args, 2, frame))
            {
                result.Error("usage: input.button <left|right|middle> <down|up> [frame]");
                return result;
            }
            SDL_Event event{};
            const bool down = args[1] == "down";
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button.windowID = window();
            event.button.button = button;
            event.button.down = down;
            event.button.clicks = 1;
            Queue.push_back({ frame, event });
            return result;
        },
    });
}

void SynthesizedInput::Advance()
{
    for (auto it = Queue.begin(); it != Queue.end();)
    {
        if (it->AtFrame > Frame)
        {
            ++it;
            continue;
        }
        SDL_Event event = it->Event;
        // A button lands where the pointer is, and a move says how far it went,
        // as a real one does.
        if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            event.motion.xrel = event.motion.x - PointerX;
            event.motion.yrel = event.motion.y - PointerY;
            PointerX = event.motion.x;
            PointerY = event.motion.y;
        }
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            event.button.x = PointerX;
            event.button.y = PointerY;
        }
        else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        {
            // A held modifier holds for everything after it, a click included.
            if (const SDL_Keymod bit = ModifierOf(event.key.key); bit != SDL_KMOD_NONE)
            {
                Modifiers = event.key.down ? static_cast<SDL_Keymod>(Modifiers | bit)
                                           : static_cast<SDL_Keymod>(Modifiers & ~bit);
                SDL_SetModState(Modifiers);
            }
            event.key.mod = Modifiers;
        }
        (void)SDL_PushEvent(&event);
        it = Queue.erase(it);
    }
    ++Frame;
}
