#pragma once

#include <camera/PresentedView.h>
#include <components/CameraComponent.h>
#include <ecs/EntityId.h>
#include <graphics/RenderExtent.h>
#include <math/Mat.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/CameraProjection.h>
#include <time/FrameClock.h>

#include <cstdint>

class FixedSimulationLoop;
class StoragePartitionSet;
class World;

struct PresentedFrame
{
    bool Valid = false;
    std::uint64_t FrameIndex = 0;
    double Alpha = 0.0;
    std::uint64_t Publication = 0;

    EntityId Camera;
    ProjectionKind Projection = ProjectionKind::Perspective;
    float NearPlane = 0.1f;
    float FarPlane = 1000.0f;
    float OrthographicHeight = 10.0f;

    Transform3f Placed;
    PresentedView View;

    RenderExtent Extent;
    Mat4 ViewMatrix = Mat4::Identity();
    Mat4 ProjectionMatrix = Mat4::Identity();
    Mat4 ViewProjection = Mat4::Identity();
};

[[nodiscard]] PresentedFrame ResolvePresentedFrame(const World& world,
                                                   EntityId camera,
                                                   RenderExtent extent,
                                                   PresentationTime presentation);

const PresentedFrame& PublishPresentedFrame(World& world,
                                            const FixedSimulationLoop& clock,
                                            PresentationTime presentation,
                                            RenderExtent extent,
                                            const StoragePartitionSet& visible);

const PresentedFrame& EnsurePresentedFrame(World& world,
                                           const FixedSimulationLoop& clock,
                                           PresentationTime presentation,
                                           RenderExtent extent,
                                           const StoragePartitionSet& visible);

[[nodiscard]] const PresentedFrame* TryPresentedFrame(const World& world);

[[nodiscard]] ViewportProjection ProjectWorldPoint(const PresentedFrame& frame,
                                                   const ViewportRect& viewport,
                                                   const Vec3d& point);
