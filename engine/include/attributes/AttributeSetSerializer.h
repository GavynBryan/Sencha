#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

//=============================================================================
// AttributeSet scene serializer
//
// Attributes persist by name (see AttributeSerialization.h), resolved through
// the AttributeRegistry stored as a world resource on the registry being
// (de)serialized. A TypeSchema cannot state that: the set holds
// registration-order ids, which are a fact about one process's startup and
// nothing a file may carry.
//
// Hand this to ComponentRegistrar::AddSerializer beside the component's Add.
//=============================================================================

[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeAttributeSetSerializer();
