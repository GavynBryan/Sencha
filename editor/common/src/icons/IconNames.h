#pragma once

#include "icons/IconId.h"

#include <string_view>

// The name an icon goes by everywhere it is stored or styled: its file is
// editor/ui/icons/<name>.svg, and authored documents show it through the
// `icon-<name>` class editor/ui/icons.rcss defines. Empty for IconId::None.
[[nodiscard]] std::string_view IconName(IconId id);
