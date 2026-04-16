#pragma once

#include <core/system/ISystem.h>

class TransformView;

class VelocityUpdateSystem2d : public ISystem
{
public:
    VelocityUpdateSystem2d(TransformView transformView);

    void Update(const FrameTime& time) override;

private:
    DataBatch<Velocity2d>& Velocities;
    TransformView<Transform2f>& Transforms;
};