#pragma once

// Support for the two-translation-unit identity test: the in-process analog of
// the cross-module case. The component's identity must not depend on which TU
// instantiated the engine templates that touch it.

#include <ecs/World.h>

#include <cstdint>

struct TwoTuComponent { int Value = 0; };
SENCHA_DECLARE_COMPONENT_TYPE(TwoTuComponent, "test.two_tu_component");

// Defined in TwoTuIdentityOtherUnit.cpp — i.e. these template instantiations
// happen in a DIFFERENT translation unit from the test that checks them.
ComponentId    RegisterTwoTuComponentInOtherUnit(World& world);
std::uint64_t  TwoTuComponentIdFromOtherUnit();
void           AddTwoTuComponentInOtherUnit(World& world, EntityId entity, int value);
