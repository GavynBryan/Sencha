#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>

// Refused: a target is an entity, and this parameter is a float.
struct SENCHA_COMPONENT("test.bad.door") BadDoor
{
};

class BadTargetTypeSystem
{
public:
    SENCHA_VERB("test.bad.open")
    VerbAdmission Open(SENCHA_TARGET("door", BadDoor) float door);
};
