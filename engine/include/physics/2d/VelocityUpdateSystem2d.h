#pragma once

#include <core/batch/DataBatch.h>
#include <math/geometry/2d/Transform2d.h>
#include <world/transform/TransformView.h>

// Stub — not yet implemented.
class VelocityUpdateSystem2d
{
public:
    explicit VelocityUpdateSystem2d(TransformView<Transform2f>& transforms);

    void Tick(float fixedDt);

private:
    TransformView<Transform2f>& Transforms;
};
