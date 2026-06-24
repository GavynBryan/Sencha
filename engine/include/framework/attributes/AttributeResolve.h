#pragma once

class World;

//=============================================================================
// ResolveAttributes
//
// Recompute every AttributeSet's Current from its Base, clamped to each
// attribute's range (from the AttributeRegistry world resource). Stage 2 has no
// modifiers; Stage 3 folds active effect modifiers in before the clamp. No-op if
// the world has no AttributeRegistry resource.
//
// Free function (not yet an EngineSchedule system) so it is testable without the
// frame-context stack; the FixedLogic wrapper is trivial to add when a game wires
// the framework into its schedule.
//=============================================================================
void ResolveAttributes(World& world);
