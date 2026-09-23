#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>

// Refused: a component is data; the operation belongs to a system.
struct SENCHA_COMPONENT("test.bad.lamp") BadLamp
{
    SENCHA_VERB("test.bad.toggle")
    VerbAdmission Toggle();
};
