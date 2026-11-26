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
    SENCHA_QUERY("test.bad.brightness")
    SENCHA_COSMETIC
    float Brightness(SENCHA_TARGET("lamp") EntityId lamp) const;
};
