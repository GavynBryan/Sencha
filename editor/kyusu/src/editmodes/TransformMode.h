#pragma once

#include <cstdint>

// Which transform gizmo is interactable. The ManipulatorSession draws and routes
// only the manipulator whose Mode() matches the active one, so the gizmos are
// switchable (pick one) instead of all shown at once.
enum class TransformMode : uint8_t
{
    Resize, // BoundsManipulator: AABB resize handles
    Move,   // TranslateManipulator: axis arrows
    Rotate, // RotateManipulator: axis rings
    Scale,  // ScaleManipulator: axis handles with end boxes
};
