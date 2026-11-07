#pragma once

#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <vector>

class World;

// Something to hear: where it happened and how far it carries. A stimulus
// with a valid Only reaches that observer alone, wherever it is, as being
// struck does.
struct PerceptionStimulus
{
    // Who made it; invalid for a sound with no maker an observer could name.
    EntityId Source{};
    Vec3d Position = Vec3d::Zero();
    float Radius = 0.0f;
    EntityId Only{};
};

// Stimuli staged for the next hearing scan, as a World resource. Staged
// before a tick's scan, a stimulus is heard that tick; after it, the next.
struct PerceptionStimuli
{
    std::vector<PerceptionStimulus> Staged;
};

// Stages on the authority; a client hears nothing. False when not staged.
bool StagePerceptionStimulus(World& world, const PerceptionStimulus& stimulus);
