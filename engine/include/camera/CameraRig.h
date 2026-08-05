#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <math/Quat.h>
#include <math/Vec.h>

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

struct CameraRig
{
    EntityId Target;
    CameraRigMode Mode = CameraRigMode::FirstPerson;
    Vec3d PivotOffset = Vec3d(0.0f, 0.7f, 0.0f);
    float Distance = 4.0f;
};

static_assert(std::is_trivially_copyable_v<CameraRig>,
              "CameraRig must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(CameraRig, "sencha.camera_rig");

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
