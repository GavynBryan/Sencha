#pragma once

#include "icons/IconId.h"

#include <string_view>

// A choice a control can show and a user can make: what it is called and
// what it looks like. Tools expose their variants as these; menus and rows
// consume them. A leaf value with no notion of who shows it.
struct CommandChoice
{
    std::string_view Label;
    IconId Icon = IconId::None;
};
