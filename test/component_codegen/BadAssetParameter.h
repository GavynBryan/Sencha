#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredValueTraits.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbResult.h>
#include <core/metadata/EnumSchema.h>
#include <ecs/EntityId.h>
#include <ecs/LeaseOwner.h>

class BadLamp
{
public:
    SENCHA_VERB("test.bad.light")
    VerbAdmission Light(SENCHA_ARG("bulb") SENCHA_ASSET(StaticMesh) float bulb);
};
