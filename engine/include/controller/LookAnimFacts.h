#pragma once

class AnimFactProviders;

// Binds LookYaw and LookPitch, in radians, from the entity's LookOrientation:
// where a body aims, for a rig that leans or sways with its aim. Yaw is the
// running total, so a rate read from it never jumps at a wrap. False when one
// already has a provider.
bool BindLookAnimFacts(AnimFactProviders& providers);
