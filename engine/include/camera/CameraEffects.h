#pragma once

#include <ecs/EntityId.h>

class FixedSimulationLoop;
class World;

// Advances the World's camera shakes and field-of-view pulses to this frame's
// presentation time and sums them for `camera`, the one about to be drawn.
// PublishPresentedFrame (camera/PresentedFrame.h) calls it once a frame, before
// it resolves the view everything is drawn and laid out with. The same clock
// and alpha give the same sums, so a second call in one frame changes nothing.
void PresentCameraEffects(World& world, EntityId camera, const FixedSimulationLoop& clock, double presentationAlpha);
