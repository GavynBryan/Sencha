#pragma once

#include <core/handle/Handle.h>

//=============================================================================
// UiScreenHandle
//
// One open authored document on a surface: a HUD, a pause menu, an inspector.
//
// Generational, like every other handle here, because a screen closing and
// another opening is the normal case and a stale handle must read as stale
// rather than as whatever took the slot.
//
// The screen -- not the package cache -- owns the asset leases its document
// needs, so document lifetime and resource lifetime end together.
//=============================================================================
using UiScreenHandle = Handle<struct UiScreenTag>;
