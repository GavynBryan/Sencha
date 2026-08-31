#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

//=============================================================================
// AbilitySet scene serializer
//
// Abilities persist by the name they were registered under, resolved through
// the AbilityRegistry stored as a world resource on the registry being
// (de)serialized. A TypeSchema cannot state that: the set holds
// registration-order ids, which are a fact about one process's startup and
// nothing a file may carry.
//
// An ability the loading process does not know is skipped rather than
// refused -- the same rule tags follow -- so content authored against a
// larger vocabulary still loads what this build understands.
//
// Hand this to ComponentRegistrar::AddSerializer beside the component's Add.
//=============================================================================

[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeAbilitySetSerializer();
