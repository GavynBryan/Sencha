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
    SENCHA_LEASE("test.bad.glow")
    VerbAdmission Glow(SENCHA_ARG("seconds") float seconds);
    SENCHA_LEASE_RELEASE("test.bad.glow")
    VerbAdmission EndGlow(const VerbInvocation& invocation, SENCHA_ARG("seconds") float seconds);
};
