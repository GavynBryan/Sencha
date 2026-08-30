#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

//=============================================================================
// GameplayTagContainer scene serializer
//
// Tags persist by name (see GameplayTagSerialization.h), resolved through the
// GameplayTagRegistry stored as a world resource on the registry being
// (de)serialized. A TypeSchema cannot state that: the container holds
// registration-order ids, which are a fact about one process's startup and
// nothing a file may carry.
//
// Hand this to ComponentRegistrar::AddSerializer beside the component's Add.
//=============================================================================

[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeGameplayTagContainerSerializer();
