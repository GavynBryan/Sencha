#pragma once

class World;
class AttributeRegistry;

//=============================================================================
// ResolveAttributes
//
// Recompute every AttributeSet's Current from its Base, clamped to each
// attribute's range (from the caller-supplied AttributeRegistry). When active
// effects are present, the effects module folds their modifiers in before the
// clamp.
//
// Free functions, so they run without the frame-context stack; a game can wrap
// them in a scheduled system.
//=============================================================================
void ResolveAttributes(World& world, const AttributeRegistry& attributes);

// Resolve primitives, so the effects module can insert its modifier fold between
// the reset and the clamp without the attributes module knowing about effects.
//   ResolveAttributes == ResetAttributesToBase + ClampAttributes.
void ResetAttributesToBase(World& world);                             // Current = Base
void ClampAttributes(World& world, const AttributeRegistry& attributes); // clamp via registry
