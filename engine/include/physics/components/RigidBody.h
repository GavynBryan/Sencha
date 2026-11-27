#pragma once

#include <core/metadata/ComponentHoldsPlacement.h>
#include <ecs/ComponentAnnotations.h>
#include <math/Vec.h>
#include <physics/PhysicsTypes.h>

// How a collider's body moves, and its motion as a save keeps it: docs/physics/rigid-bodies.md.
struct SENCHA_COMPONENT("sencha.physics.rigid_body")
       SENCHA_SCHEMA("RigidBody")
       SENCHA_SCENE_CHUNK("RGBD")
       SENCHA_SAVED
RigidBody
{
    SENCHA_FIELD("motion")
    SENCHA_TOOLTIP("Static never moves, kinematic is moved by its transform, dynamic falls and is pushed.")
    BodyMotion Motion = BodyMotion::Dynamic;

    SENCHA_FIELD("mass")
    SENCHA_TOOLTIP("Kilograms. Zero or less takes the mass from the shape.")
    float Mass = 1.0f;

    SENCHA_FIELD("linear_velocity")
    SENCHA_LABEL("Velocity")
    SENCHA_TOOLTIP("Metres per second in world space; what a dynamic body starts moving at.")
    Vec3d LinearVelocity = Vec3d::Zero();

    SENCHA_FIELD("gravity_scale")
    SENCHA_TOOLTIP("How much of the world's gravity it falls with.")
    float GravityScale = 1.0f;

    SENCHA_FIELD("angular_velocity")
    SENCHA_LABEL("Spin")
    SENCHA_TOOLTIP("Radians per second about each world axis; what a dynamic body starts turning at.")
    Vec3d AngularVelocity = Vec3d::Zero();

    SENCHA_FIELD("linear_damping")
    SENCHA_TOOLTIP("Fraction of its velocity a body loses each second.")
    float LinearDamping = 0.05f;

    SENCHA_FIELD("angular_damping")
    SENCHA_TOOLTIP("Fraction of its spin a body loses each second.")
    float AngularDamping = 0.05f;

    SENCHA_FIELD("asleep")
    SENCHA_TOOLTIP("Starts settled: a dynamic body that does not move until something touches it.")
    bool Asleep = false;
};

template <>
struct ComponentHoldsPlacement<RigidBody>
{
    static bool Holds(const RigidBody& body) { return body.Motion == BodyMotion::Dynamic; }
};

#if !defined(SENCHA_CODEGEN)
#  include <physics/components/RigidBody.sencha.h>
#endif
