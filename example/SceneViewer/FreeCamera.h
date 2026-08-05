#pragma once

#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <input/InputActionState.h>
#include <input/InputBindingCache.h>
#include <math/geometry/3d/Transform3d.h>

class DataAssetCache;

// The actions a fly camera reads, resolved once from the host's profile.
struct FlyCameraActions
{
    InputActionId Look;
    InputActionId Move;
    InputActionId Vertical;
    InputActionId Fast;
    InputActionId LookEnable;
    InputActionId DumpTrace;
};

// Host fly-camera: mouse-look while the look control is held, planar move plus
// a vertical axis. Pure action-driven transform mutation, no gameplay state. The
// host binds one to the active camera so any cooked scene is viewable even when
// it carries no camera of its own.
struct FreeCamera
{
    EntityId Entity;
    FlyCameraActions Actions;
    float Yaw = 0.0f;
    float Pitch = 0.0f;
    float MoveSpeed = 4.0f;
    float FastMultiplier = 4.0f;
    // A tool-side multiplier on top of the binding, so the debug UI can tune
    // look feel live without editing an asset.
    float MouseSensitivity = 0.0025f;
    bool LookHeld = false;

    void UpdateLook(const InputActionView& input);
    void TickFixed(const InputActionView& input, World& world, float fixedDt);
    void ApplyRotation(World& world) const;
};

// Registers the host's own fly-camera controls and returns the profile.
//
// Built in code rather than loaded from an asset because these are the tool's
// controls, not the content's: a viewer pointed at arbitrary cooked output must
// still be able to move its camera, whatever that content root happens to hold.
// It goes through the same registry, cache, and resolve path as authored input.
InputProfileHandle RegisterFlyCameraInput(DataAssetCache& dataAssets);
