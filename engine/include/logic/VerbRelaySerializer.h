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
// The scene form is lossless. `bindings` is the asset path and `binding` the
// key's text, both kept as authored in the World's VerbRelayAuthoring whether
// or not the asset loaded, so a scene opened without its gameplay package
// saves the reference it came with. A key nothing ever spelled is written as
// `binding_hash`, sixteen hex digits, a different field on purpose: a key is
// never taken for a hash by its spelling, and a library may name a record
// "deadbeefdeadbeef" if it likes.
//=============================================================================
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeVerbRelaySerializer();
