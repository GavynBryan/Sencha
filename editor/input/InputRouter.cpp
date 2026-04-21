#include "InputRouter.h"

void InputRouter::AddHandler(Handler handler)
{
    Handlers.push_back(std::move(handler));
}

InputConsumed InputRouter::Route(const InputEvent& event) const
{
    for (const auto& handler : Handlers)
    {
        if (handler(event) == InputConsumed::Yes)
            return InputConsumed::Yes;
    }
    return InputConsumed::No;
}
