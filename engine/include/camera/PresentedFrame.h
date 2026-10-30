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

//=============================================================================
// PresentedFrame
//
// The camera view this frame is drawn with, resolved once and shared. Render
// extraction builds the frame's camera from it, and anything laid over the
// frame at a place in the world -- a marker, a leader line, a reticle that
// shows the shake -- reads the same snapshot, so the two cannot disagree by an
// effect, an interpolation or a frame.
//
// It is published after the frame's ticks and frame-update systems, before
// PresentationUpdate systems and UI layout, and it is a copy: resolving it reads
// the World and writes nothing a simulation-side system reads. WorldTransform
// is not touched -- presentation propagation still runs during extraction, as
// before -- so audio, gameplay and anything else on the simulation's side of
// the frame sees exactly what it saw before this existed. The one World state
// publication advances is the camera effects' presented sums, which only
// presentation reads.
//
// Readers treat it as read-only. A change made after publication -- a UI
// action that switches the active camera, say -- shows next frame, except that
// extraction resolves again when the camera it would draw is no longer the one
// the snapshot holds, so the picture never shows a camera that is not active.
//=============================================================================
struct PresentedFrame
{
    // False when nothing is presented: no active camera, one not visible this
    // frame, or no extent to draw into (a headless host).
    bool Valid = false;
    // PresentationTime::FrameIndex of the frame this describes.
    std::uint64_t FrameIndex = 0;
    double Alpha = 0.0;
    // How many times a frame has been published into this World. Once a frame
    // when the frame loop publishes and extraction reuses it; a frame that
    // advanced it twice was resolved again after overlays laid out against it.
    std::uint64_t Publication = 0;

    EntityId Camera;
    ProjectionKind Projection = ProjectionKind::Perspective;
    float NearPlane = 0.1f;
    float FarPlane = 1000.0f;
    float OrthographicHeight = 10.0f;

    // The camera's pose blended to presentation time, before presented effects.
    Transform3f Placed;
    // What is drawn: Placed moved by the presented shake, the field of view
    // scaled by the presented pulses.
    PresentedView View;

    RenderExtent Extent;
    Mat4 ViewMatrix = Mat4::Identity();
    Mat4 ProjectionMatrix = Mat4::Identity();
    Mat4 ViewProjection = Mat4::Identity();
};

// The view `camera` is drawn with at `presentation`, into `extent`, from the
// camera effects as last presented. Writes nothing. Invalid for a camera that is
// dead or has no CameraComponent, or an empty extent.
[[nodiscard]] PresentedFrame ResolvePresentedFrame(const World& world,
                                                   EntityId camera,
                                                   RenderExtent extent,
                                                   PresentationTime presentation);

// Presents the camera effects for the World's active camera and stores the
// frame's PresentedFrame as a World resource, replacing the last one. A camera
// outside `visible` publishes an invalid frame: it is not drawn. With an empty
// extent nothing is drawn either, and the effects are left unpresented.
const PresentedFrame& PublishPresentedFrame(World& world,
                                            const FixedSimulationLoop& clock,
                                            PresentationTime presentation,
                                            RenderExtent extent,
                                            const StoragePartitionSet& visible);

// The published frame when it describes this frame, this extent and the camera
// now active; published again otherwise. Extraction calls this, so a host that
// never reached the publication point still draws.
const PresentedFrame& EnsurePresentedFrame(World& world,
                                           const FixedSimulationLoop& clock,
                                           PresentationTime presentation,
                                           RenderExtent extent,
                                           const StoragePartitionSet& visible);

// The last published frame, or null before the first. Check FrameIndex when it
// matters which frame it describes.
[[nodiscard]] const PresentedFrame* TryPresentedFrame(const World& world);

// Where `point` lands on the frame, in `viewport`'s units. Behind the camera
// for an invalid frame.
[[nodiscard]] ViewportProjection ProjectWorldPoint(const PresentedFrame& frame,
                                                   const ViewportRect& viewport,
                                                   const Vec3d& point);
