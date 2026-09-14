#include <input/PlatformEventRouter.h>

#include <input/SdlGamepadCapture.h>
#include <input/SdlInputCapture.h>

void PlatformEventRouter::AddConsumer(std::string_view name, Consumer consumer)
{
    if (!consumer)
        return;

    Consumers.push_back(Entry{ std::string(name), std::move(consumer) });
}

std::string_view PlatformEventRouter::ConsumerName(std::size_t index) const
{
    if (index >= Consumers.size())
        return {};

    return Consumers[index].Name;
}

bool PlatformEventRouter::Route(const SDL_Event& event,
                                InputFrame& frame,
                                SdlGamepadCapture* gamepads)
{
    // First, and not inside any condition. See the header.
    SdlInputCapture::Accept(frame, event);
    if (gamepads != nullptr)
        gamepads->Accept(frame, event);

    for (const Entry& entry : Consumers)
    {
        if (entry.Handle(event))
            return true;
    }

    return false;
}
