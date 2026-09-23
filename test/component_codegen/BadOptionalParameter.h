#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>

// Refused: an argument that may be absent says so in its type.
class BadOptionalParameterSystem
{
public:
    SENCHA_VERB("test.bad.throw")
    VerbAdmission Throw(SENCHA_ARG("target") SENCHA_OPTIONAL EntityId target);
};
