#pragma once

// Why a carve refused. One vocabulary for the whole operation: the face frame,
// the surround decomposition, and both kernels report through it, so a refusal
// can say what went wrong instead of leaving the tool to guess from a face
// count that did not change.
enum class CarveStatus
{
    Ok,
    InvalidOutline,      // not simple, not CCW, or fewer than three points
    OutsideHost,         // the outline leaves the face it is being cut into
    DegenerateEdge,      // an outline edge is shorter than the weld tolerance
    NonPlanarFace,       // the host is not flat enough to be a 2D workspace
    NoOppositeFace,      // a through-carve found nothing to come out of
    InvalidProjection,   // the projection is singular: direction parallel to the target plane
    NonPlanarTunnelWall, // snapping the projected outline bent a wall out of plane
    ChannelCrossesHole,  // the side plane a channel crosses is not simply connected
    HostNotConvex,       // the shape reaches a coplanar face that is not convex
    TopologyFailure,     // the mesh edit could not be completed
};

// A short phrase for the hover readout, naming the cause rather than restating
// that something failed.
[[nodiscard]] const char* CarveStatusText(CarveStatus status);
