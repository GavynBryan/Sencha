#pragma once

class AnimFactProviders;

// Binds the core fact schema's Grounded, Speed and VerticalSpeed slots, the
// planar MoveForward and MoveRight, and StrideCycle and StrideSpeed from the
// character's StrideCycle; false when one already has a provider. Their inputs
// are owner-only on the wire or counted locally, so a client fills only its
// locally controlled character.
bool BindMovementAnimFacts(AnimFactProviders& providers);
