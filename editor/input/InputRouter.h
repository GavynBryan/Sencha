#pragma once

#include "InputEvent.h"

#include <functional>
#include <vector>

class InputRouter
{
public:
    using Handler = std::function<InputConsumed(const InputEvent&)>;

    void AddHandler(Handler handler);
    InputConsumed Route(const InputEvent& event) const;

private:
    std::vector<Handler> Handlers;
};
