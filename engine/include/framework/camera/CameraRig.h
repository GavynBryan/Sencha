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
// function selecting on a value, not three code paths a game swaps between. Yaw
// and Pitch accumulate from look input; PivotOffset is the eye/look point above
// the target; Distance is the third-person boom length. Fixed leaves the authored
// camera pose untouched and only carries the target relationship.
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
    Vec3d PivotOffset = Vec3d(0.0f, 1.6f, 0.0f);
    float Distance = 4.0f;
    float Yaw = 0.0f;
    float Pitch = 0.0f;
    float MinPitch = -1.4f;
    float MaxPitch = 1.4f;
    float Sensitivity = 0.0025f;
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
// look direction; Fixed returns Override == false.
CameraPose ComputeCameraPose(const CameraRig& rig, const Vec3d& targetWorldPosition);
