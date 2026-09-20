#pragma once

#include <assets/data/DataAssetHandle.h>
#include <authored/VerbBindingData.h>
#include <authored/VerbId.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <world/ComponentAssetOwnership.h>
#include <world/serialization/SceneFieldCodec.h>

#include <string>
#include <string_view>

//=============================================================================
// VerbRelay
//
// An entity that, when activated, invokes one authored binding.
//
// Data only: which binding asset, and which record in it. What activates a
// relay is a native gameplay path handing VerbRelaySystem the entity and its
// typed inputs; what the relay does is the binding's business, resolved against
// the World's catalog by VerbRelayBindingStore at the fixed-logic drain. The
// component itself holds no resolved state, so it is safe to serialize, copy
// between partitions, and stream out and back in.
//
// A relay is a placed thing, so it names its binding by the same stable key a
// binding asset carries -- hashed, the way a persistent entity identity is a
// number in a component and hex digits in a document.
//=============================================================================

struct SENCHA_COMPONENT("sencha.verb_relay")
       SENCHA_SCHEMA("verb_relay")
       SENCHA_SCENE_CHUNK("VRLY")
VerbRelay
{
    SENCHA_FIELD("bindings")
    SENCHA_DATA_ASSET(kVerbBindingsTypeName)
    SENCHA_LABEL("Bindings")
    SENCHA_TOOLTIP("The authored binding set this relay invokes from.")
    DataAssetHandle Bindings{};

    // Which record in that set. Persisted as sixteen lowercase hex digits of
    // the key's hash; the set itself carries the key's text, which is what a
    // diagnostic resolves this back to.
    SENCHA_FIELD("binding")
    SENCHA_LABEL("Binding")
    SENCHA_TOOLTIP("The key of the binding to invoke, hashed.")
    VerbBindingKey Binding;
};

// The scene form of a hashed binding key: sixteen lowercase hex digits, strict
// on load for the same reason a persistent entity id is -- a malformed key
// that parsed leniently would resolve to no binding and read as a relay that
// does nothing.
template <>
struct SceneFieldCodec<VerbBindingKey>
{
    static bool Save(IWriteArchive&, std::string_view, VerbBindingKey,
                     SceneSerializationContext&);
    static bool Load(IReadArchive&, std::string_view, VerbBindingKey&,
                     SceneSerializationContext&);
};

[[nodiscard]] std::string VerbBindingKeyToString(VerbBindingKey key);
[[nodiscard]] bool VerbBindingKeyFromString(std::string_view text, VerbBindingKey& out);

#if !defined(SENCHA_CODEGEN)
#  include <logic/VerbRelay.sencha.h>
#endif

// The relay owns one reference to its binding set for as long as it carries
// the component, which is what keeps the set resident between the scene that
// placed the relay and the drain that reads it.
template <>
struct ComponentTraits<VerbRelay> : SchemaAssetOwnership<VerbRelay> {};
