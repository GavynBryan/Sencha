#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>

// Refused: a default becomes the schema's default, so it is a constant.
inline float BadDefaultForce() { return 2.0f; }

class BadNonConstantDefaultSystem
{
public:
    SENCHA_VERB("test.bad.shove")
    VerbAdmission Shove(SENCHA_ARG("force") float force = BadDefaultForce());
};
