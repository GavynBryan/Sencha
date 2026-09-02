#pragma once

#include <ecs/ComponentAnnotations.h>

// A game-defined component: the example of how a game adds its own data. Its
// annotations make it serialize through the cook and show up, editable, in the
// editor inspector with no editor code naming it. SpinSystem (in
// TemplateGame.cpp) rotates entities that carry it. Replace this with your own
// components.
struct SENCHA_COMPONENT("spin")
       SENCHA_SCHEMA("spin")
       SENCHA_SCENE_CHUNK("SPIN")
SpinComponent
{
    SENCHA_FIELD("radians_per_second")
    float RadiansPerSecond = 1.0f;
};

#if !defined(SENCHA_CODEGEN)
#  include <SpinComponent.sencha.h>
#endif
