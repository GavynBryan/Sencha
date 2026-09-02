#pragma once

#include <core/metadata/EnumSchema.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <math/Quat.h>
#include <math/Vec.h>

#include <array>
#include <cstdint>
#include <type_traits>

//=============================================================================
// CameraRig
//
// How a camera entity is placed relative to a target each frame. The mode is a
// data field: first-person, third-person boom, and fixed-angle are one pose
// function selecting on a value, not three code paths a game swaps between.
// PivotOffset is the eye/look point above the target; Distance is the
// third-person boom length. Fixed leaves the authored camera pose untouched and
// only carries the target relationship.
//
// The rig does not own where the player is aiming. That is the target's
// LookOrientation, which the character steers along and an AI could drive
// instead; the camera is one of its readers, passed the orientation to present.
//
// This is backend-free data plus the pure pose math (the framework isolation
// rule). The system that reads the active camera and writes its transform lives
// in the composition layer, where render is reachable.
//=============================================================================
enum class CameraRigMode : std::uint8_t
{
    FirstPerson,
    ThirdPerson,
    Fixed,
};

// Authored by name, because which of these a camera uses is the difference
// between one game and another built on the same pawn.
template <>
struct EnumSchema<CameraRigMode>
{
    static constexpr std::array Values = {
        EnumValue{ CameraRigMode::FirstPerson, "first_person", "First person",
                   "The view sits where the camera is placed and turns with "
                   "the body's aim." },
        EnumValue{ CameraRigMode::ThirdPerson, "third_person", "Third person",
                   "The view orbits the body at a distance, looking back at "
                   "it along the aim." },
        EnumValue{ CameraRigMode::Fixed, "fixed", "Fixed",
                   "The camera stays where it was placed and is not moved by "
                   "what it watches." },
    };
};

struct SENCHA_COMPONENT("sencha.camera_rig") CameraRig
{
    EntityId Target;
    CameraRigMode Mode = CameraRigMode::FirstPerson;
    Vec3d PivotOffset = Vec3d(0.0f, 0.7f, 0.0f);
    float Distance = 4.0f;
};

static_assert(std::is_trivially_copyable_v<CameraRig>,
              "CameraRig must be trivially copyable to live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <camera/CameraRig.sencha.h>
#endif

//=============================================================================
// CameraPose
//
// Camera local pose computed from a rig and its target's world position. Override
// is false for Fixed mode: the caller leaves the authored camera transform alone.
//=============================================================================
struct CameraPose
{
    Vec3d Position = Vec3d::Zero();
    Quatf Rotation = Quatf::Identity();
    bool Override = false;
};

// FirstPerson sits at the pivot; ThirdPerson swings a boom of Distance behind the
// look direction; Fixed returns Override == false. Yaw and pitch come from the
// target's LookOrientation.
CameraPose ComputeCameraPose(const CameraRig& rig,
                             const Vec3d& targetWorldPosition,
                             float yaw,
                             float pitch);

// The entity this rig's camera must not draw, or an invalid id when it may draw
// everything. A first-person camera sits inside its target, so drawing the target
// fills the view with the inside of its own geometry. Third-person and fixed rigs
// look at the target from outside and exclude nothing.
EntityId CameraRigExcludedEntity(const CameraRig& rig);
