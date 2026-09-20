#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

//=============================================================================
// VerbRelay scene serializer
//
// The component holds a data-asset handle and a hashed key; the scene holds
// an asset path and the key's text. Neither is the other's persisted form, so
// the generic schema serializer cannot write this component, and it carries a
// serializer of its own the way MovementTuningSource does.
//
// The key round-trips as text whenever the binding asset is resident and still
// holds the record, so a rename or a search in the scene file sees the name
// the author gave it. When it does not -- the record was removed, or the asset
// did not load -- the hash is written as sixteen hex digits, which is the only
// truth the component has left, and reads back to the same hash. Either form
// loads.
//=============================================================================
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeVerbRelaySerializer();
