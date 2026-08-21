#pragma once

#include <render/ProjectedShadowFramePolicy.h>

class ConsoleRegistry;

//=============================================================================
// Projected-shadow console surface, owned by the feature it tunes.
//
// Registration and the budgets read live here so the app layer's builtin
// registration stays a composition root that DELEGATES, not a ledger of every
// subsystem's knob names and defaults. The RenderLight fields themselves are
// read by ApplyRendererCVars like every other render.* tunable.
//=============================================================================

// Registers render.shadow.projected and its tunables. Called once from the
// engine's console composition root.
void RegisterProjectedShadowCVars(ConsoleRegistry& registry);

// The frame budgets, readable by every host that renders a scene; defaults
// come from the struct when the registry or a cvar is absent.
[[nodiscard]] ProjectedShadowBudgets ReadProjectedShadowBudgets(
    const ConsoleRegistry* registry);
