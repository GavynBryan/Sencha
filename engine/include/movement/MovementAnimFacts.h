#pragma once

class AnimFactProviders;

// Binds the core fact schema's movement slots to the movement components that
// hold them: Grounded (stable support), Speed (planar velocity), VerticalSpeed.
// Dead is gameplay's to bind; nothing in movement knows it. False when any of
// the slots already has a provider, which is a conflict to resolve rather than
// a binding to overwrite.
//
// The velocity and support the providers read are owner-only on the wire, so
// on a client these fill the locally controlled character and leave remote
// ones untouched.
bool BindMovementAnimFacts(AnimFactProviders& providers);
