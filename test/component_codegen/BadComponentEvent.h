#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>

// Refused: state and an announcement are two declarations.
struct SENCHA_COMPONENT("test.bad.both") SENCHA_EVENT("test.bad.both") BadBoth
{
};
