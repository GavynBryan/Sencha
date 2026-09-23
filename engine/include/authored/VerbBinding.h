#pragma once

#include <authored/AuthoredValue.h>
#include <authored/VerbId.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// Verb bindings: what content says, and what a World turned it into
//
// Two types for one authored record, because they have different lifetimes.
//
// VerbBindingDesc is what a shared asset holds. It names a verb, never an id,
// and keeps its references as typed unresolved descriptors so a staging worker
// can report what the binding depends on without touching a World. One compiled
// .sdata can therefore be instantiated into several Worlds at once and mean the
// right thing in each.
//
// CompiledVerbBinding is what one World turned that into: ids, revisions,
// constants already converted, and a slot index per declared input. It carries
// the catalog it resolved against, so a binding held across a World teardown
// fails its check rather than dispatching into whichever registry took the old
// one's address.
//=============================================================================

// Where one argument's value comes from. A reference's kind is explicit in the
// authored file rather than inferred from the verb's schema, because the first
// pass runs off the owner thread with no catalog to consult and still has to
// name the assets this binding will need.
enum class VerbArgumentSource : std::uint8_t
{
    // A JSON literal covering the value family a schema can describe:
    // bool, int, float, string, enum choice, vector, record, array, and the
    // null that means an absent optional.
    Literal,
    // An asset:// path. An eager dependency of the binding asset.
    Asset,
    // An asset:// path naming a .sdata of the subtype the schema requires.
    // Also an eager dependency.
    DataAsset,
    // A gameplay tag name, resolved against the bound World's tag registry.
    // Not a dependency: tags are registration-order values, not content.
    Tag,
    // A persistent entity identity, sixteen lowercase hex digits, resolved
    // against the bound World's persistent entity index.
    Entity,
    // One of the binding's declared input slots. The producer fills it.
    Input,
};

struct VerbBindingArgument
{
    // The argument's name in the verb's record root.
    std::string Key;

    VerbArgumentSource Source = VerbArgumentSource::Literal;

    // Source == Literal.
    JsonValue Literal;

    // Source == Asset, DataAsset, Tag, Entity: the path, tag name, or identity
    // text exactly as authored. Source == Input: the declared slot's name.
    std::string Text;
};

// One authored binding record, World-independent.
struct VerbBindingDesc
{
    // The authored identity. A component or runtime table holds KeyId; the
    // string stays here so a diagnostic can always say what it meant.
    std::string Key;
    VerbBindingKey KeyId;

    // The verb's qualified name. Never an id: ids are World-local and this
    // record is shared.
    std::string VerbName;

    // The slots a producer fills, in the order a producer supplies them.
    std::vector<std::string> Inputs;

    std::vector<VerbBindingArgument> Arguments;
};

// The compiled value of one `authored.bindings` .sdata. Contains no verb ids,
// tag ids, entity handles, runtime pointers, or anything bound to one host:
// two Worlds share this and resolve it independently.
struct VerbBindingLibrary
{
    // Authored order, which is what a diagnostic and a fixture read back.
    std::vector<VerbBindingDesc> Bindings;

    [[nodiscard]] const VerbBindingDesc* Find(std::string_view key) const;
    [[nodiscard]] const VerbBindingDesc* Find(VerbBindingKey key) const;
};

// One argument an input fills, and what that argument accepts. Copied out of
// the catalog rather than pointed at: a definition vector reallocates when the
// next provider declares something, and a compiled binding outlives that.
struct VerbInputDestination
{
    std::size_t ArgumentSlot = 0;
    DataFieldSchema Expected;
};

// One declared input slot, compiled. A producer supplies one value per input;
// the value may fill several arguments -- an effect applied to the entity that
// caused it names that entity twice -- and it is checked against every one.
struct VerbCompiledInput
{
    std::string Name;
    std::vector<VerbInputDestination> Destinations;
};

// One binding resolved against one catalog.
//
// Invoking this does no name resolution, no schema traversal, no JSON, and no
// asset loading: the constants are already typed values, and the inputs are
// already slot indices.
struct CompiledVerbBinding
{
    VerbCatalogId Catalog;
    VerbId Verb;
    VerbContractRevision Revision;

    VerbBindingKey Key;
    // Diagnostics only. Owned, because the library it was compiled from can be
    // replaced by a reload while this binding is still being used.
    std::string KeyText;

    // Every argument slot, with constants and defaults already filled. Input
    // slots are left empty for the producer's values.
    AuthoredArguments Constants;

    // In the order the binding declared them, which is the order a producer
    // supplies values.
    std::vector<VerbCompiledInput> Inputs;

    // Whether every asset reference this binding holds was checked against
    // real asset metadata when it compiled. False when it holds one and the
    // environment had none to check against: the binding compiled on its
    // authored form, and an inspector must say so rather than call it
    // resolved. A binding with no asset references is checked by vacuity.
    bool ReferencesChecked = true;

    [[nodiscard]] bool IsValid() const { return Catalog.IsValid() && Verb.IsValid(); }
};
