#include "brush/CarveStatus.h"

const char* CarveStatusText(CarveStatus status)
{
    switch (status)
    {
    case CarveStatus::Ok:                  return "ok";
    case CarveStatus::InvalidOutline:      return "the carve shape is not a simple outline";
    case CarveStatus::OutsideHost:         return "the carve leaves the surface";
    case CarveStatus::DegenerateEdge:      return "the carve has an edge too short to keep";
    case CarveStatus::NonPlanarFace:       return "this face is not flat enough to carve";
    case CarveStatus::NoOppositeFace:      return "nothing on the far side to carve through to";
    case CarveStatus::InvalidProjection:   return "the far face is edge-on to this one";
    case CarveStatus::NonPlanarTunnelWall: return "the tunnel would need a bent wall";
    case CarveStatus::ChannelCrossesHole:  return "the surface it cuts through already has a hole";
    case CarveStatus::HostNotConvex:       return "the carve and the face it crosses into are both concave";
    case CarveStatus::TopologyFailure:     return "the carve could not be completed";
    }
    return "the carve could not be completed";
}
