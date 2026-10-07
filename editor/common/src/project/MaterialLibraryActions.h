#pragma once

#include "actions/ActionCatalog.h"

// The project's material library as actions every workspace that uses it
// offers: browsing it for a material, and reading it again from disk.
[[nodiscard]] ActionDefinition MaterialExplorerAction();
[[nodiscard]] ActionDefinition MaterialRescanAction();
