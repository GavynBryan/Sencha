#pragma once

#include <core/metadata/Field.h>
#include <ecs/ComponentAnnotations.h>
#include <math/Vec.h>

// The generator's golden input: one component exercising every annotation.
// Its expected output is GoldenComponent.sencha.h.expected, compiled by
// GoldenComponentTests.cpp and byte-compared once the generator can run.

struct SENCHA_COMPONENT("test.codegen.golden")
       SENCHA_SCHEMA("CodegenGolden")
       SENCHA_SCENE_CHUNK("GOLD")
       SENCHA_REPLICATED
       SENCHA_PREDICTED
       SENCHA_NON_REMOVABLE
       SENCHA_VISUAL_MESH("golden.glb")
CodegenGolden
{
    SENCHA_FIELD("speed")
    SENCHA_OWNER_ONLY
    SENCHA_QUANTIZE(0, 100, 16)
    float Speed = 4.0f;

    SENCHA_FIELD("tint")
    SENCHA_COLOR
    SENCHA_LABEL("Tint")
    SENCHA_TOOLTIP("Multiplied into the albedo.")
    Vec3d Tint = Vec3d{ 1.0f, 1.0f, 1.0f };

    SENCHA_FIELD("pitch")
    SENCHA_DEGREES
    float Pitch = 0.0f;

    SENCHA_FIELD("derived")
    SENCHA_LOCAL_ONLY
    float Derived = 0.0f;

    // Untagged: not part of the schema, and adding one must never change the
    // scene format or the replication layout.
    float Scratch = 0.0f;
};
