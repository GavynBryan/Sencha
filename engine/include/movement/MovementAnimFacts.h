#pragma once

class AnimFactProviders;

// Binds the core fact schema's Grounded, Speed and VerticalSpeed slots; false
// when one already has a provider. Their inputs are owner-only on the wire, so a
// client fills only its locally controlled character.
bool BindMovementAnimFacts(AnimFactProviders& providers);
