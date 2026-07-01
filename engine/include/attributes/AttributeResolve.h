#pragma once

class World;

//=============================================================================
// ResolveAttributes
//
// Recompute every AttributeSet's Current from its Base, clamped to each
// attribute's range (from the AttributeRegistry world resource). When active
// effects are present, the effects module folds their modifiers in before the
// clamp. No-op if the world has no AttributeRegistry resource.
//
// Free functions, so they run without the frame-context stack; a game can wrap
// them in a scheduled system.
//=============================================================================
void ResolveAttributes(World& world);

// Resolve primitives, so the effects module can insert its modifier fold between
// the reset and the clamp without the attributes module knowing about effects.
//   ResolveAttributes == ResetAttributesToBase + ClampAttributes.
void ResetAttributesToBase(World& world); // Current = Base
void ClampAttributes(World& world);       // Current = clamp(Current) via the registry
