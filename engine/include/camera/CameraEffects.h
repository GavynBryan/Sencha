#pragma once

#include <ecs/EntityId.h>

class FixedSimulationLoop;
class World;

// Advances the World's camera shakes and field-of-view pulses to this frame's
// presentation time and sums them for `camera`, the one about to be drawn.
// Render extraction calls it just before CameraRenderDataSystem::Build reads them.
void PresentCameraEffects(World& world, EntityId camera, const FixedSimulationLoop& clock, double presentationAlpha);
