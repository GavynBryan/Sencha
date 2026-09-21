#pragma once

#include <assets/data/DataAssetHandle.h>
#include <authored/VerbBindingData.h>
#include <authored/VerbId.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <world/ComponentAssetOwnership.h>

#include <string>
#include <string_view>
#include <unordered_map>

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
// binding asset carries. The component holds the key's hash, since a component
// cannot carry a string; the scene holds the key's text, which is what an
// author renames and a diagnostic reads. VerbRelaySerializer is where the two
// meet, which is why this component declares no scene chunk of its own.
//=============================================================================

struct SENCHA_COMPONENT("sencha.verb_relay")
       SENCHA_SCHEMA("verb_relay")
VerbRelay
{
    SENCHA_FIELD("bindings")
    SENCHA_DATA_ASSET(kVerbBindingsTypeName)
    SENCHA_LABEL("Bindings")
    SENCHA_TOOLTIP("The authored binding set this relay invokes from.")
    DataAssetHandle Bindings{};

    // Which record in that set, by the hash of its key. The scene form is the
    // key's text; see VerbRelaySerializer.
    SENCHA_FIELD("binding")
    SENCHA_LABEL("Binding")
    SENCHA_TOOLTIP("The key of the binding to invoke, hashed.")
    VerbBindingKey Binding;
};

// What each loaded relay's scene said, kept beside the component because the
// component cannot hold it: the asset path as authored, and the key's text.
// Read back when the relay is saved, so a scene opened without its gameplay
// package -- the asset absent, the load refused -- still saves the reference
// it came with instead of the invalid handle it ended up holding. Also the
// spelling behind each hashed key, so a relay that loaded from a hash saves
// as the text some other relay taught this World.
struct VerbRelayAuthoring
{
    struct Record
    {
        std::string BindingsPath;
        std::string KeyText;
    };

    std::unordered_map<EntityId, Record, EntityIdHash> ByEntity;
    std::unordered_map<std::uint64_t, std::string> KeyTexts;

    void Remember(EntityId entity, std::string bindingsPath, std::string keyText,
                  VerbBindingKey key)
    {
        if (!keyText.empty() && key.IsValid())
            KeyTexts[key.Value] = keyText;
        ByEntity[entity] = Record{ std::move(bindingsPath), std::move(keyText) };
    }

    [[nodiscard]] const Record* Find(EntityId entity) const
    {
        const auto it = ByEntity.find(entity);
        return it == ByEntity.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const std::string* FindKeyText(VerbBindingKey key) const
    {
        const auto it = KeyTexts.find(key.Value);
        return it == KeyTexts.end() ? nullptr : &it->second;
    }
};

// The hash's text form, for a scene whose key nothing can spell: sixteen
// lowercase hex digits, strict on load.
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
