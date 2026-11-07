#pragma once

#include <core/text/InlineString.h>
#include <ecs/ComponentAnnotations.h>
#include <physics/CollisionShape.h>

#include <cstdint>

// A part of an entity a strike can land on: a shape in its own entity's frame,
// named by a gameplay tag. It belongs to its entity or to its entity's parent,
// so a character's head is a child placed where the head is. It collides with
// nothing and no query sees it; a strike resolves against it afterwards.
struct SENCHA_COMPONENT("sencha.hit_region")
       SENCHA_SCHEMA("HitRegion")
       SENCHA_SCENE_CHUNK("HITR")
HitRegion
{
    SENCHA_FIELD("region")
    SENCHA_TOOLTIP("The gameplay tag naming this part, as whatever reads strikes knows it.")
    InlineString<64> Region;

    SENCHA_FIELD("shape")
    SENCHA_TOOLTIP("Where the part is, in this entity's frame.")
    CollisionShape Shape;

    SENCHA_FIELD("priority")
    SENCHA_TOOLTIP("Which of two parts a ray enters at the same distance wins.")
    std::int32_t Priority = 0;
};

#if !defined(SENCHA_CODEGEN)
#  include <physics/components/HitRegion.sencha.h>
#endif
