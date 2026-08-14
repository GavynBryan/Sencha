#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <functional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>

class World;

//=============================================================================
// NetSpawnRecipe
//
// What a replicated entity is on the machine receiving it.
//
// A snapshot carries values, so an entity built from one arrives with exactly
// the components that replicate and nothing else -- no derived columns, no
// presentation, no local scaffolding. That is most of what an entity is. Left
// to each game to reassemble by hand, the difference is invisible when it is
// wrong: the entity exists, its state is correct, and nothing draws it.
//
// So the authority names a recipe and the receiver builds the rest from its own
// definition of it. The wire carries the name, never the recipe: both machines
// already have the same build and the same content, which is the assumption the
// whole content posture rests on.
//
// This is the interim form of the prefab spawn encoding the protocol reserves
// (networking.md 6.1). When prefab assets land, the recipe id becomes an
// AssetId plus overrides and this registry goes away; the shape of the seam --
// authority names, receiver instantiates -- is deliberately the same one, so
// that is a substitution rather than a redesign.
//=============================================================================

// Small because it rides in every spawn, and dense because both sides get it
// from the same registration order. Zero means no recipe: an entity that is
// nothing but its replicated state, which is a legitimate thing to be.
using NetSpawnRecipeId = std::uint16_t;
inline constexpr NetSpawnRecipeId kNetNoSpawnRecipe = 0;

//-----------------------------------------------------------------------------
// The component that carries it. Replicated like any other data, so a spawn
// needs no message of its own.
//
// Read once, when the entity first arrives: a recipe describes how to build
// something, not what it currently is, and re-running one over a live entity
// has no defined meaning against the components it already added. Changing the
// id on a live entity therefore replicates the new value and builds nothing.
//-----------------------------------------------------------------------------
struct NetSpawnRecipe
{
    NetSpawnRecipeId Id = kNetNoSpawnRecipe;
};

static_assert(std::is_trivially_copyable_v<NetSpawnRecipe>,
              "NetSpawnRecipe must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(NetSpawnRecipe, "sencha.net_spawn_recipe");

template <>
struct TypeSchema<NetSpawnRecipe>
{
    static constexpr std::string_view Name = "NetSpawnRecipe";
    // How a client builds the entity in the first place, so it has to arrive
    // in the same snapshot that first mentions it.
    static constexpr bool Replicated = true;

    static auto Fields()
    {
        return std::tuple{
            MakeField("id", &NetSpawnRecipe::Id),
        };
    }
};

//-----------------------------------------------------------------------------
// NetSpawnRecipes
//
// The receiver's definitions, registered by the game. A recipe completes an
// entity that already holds its replicated state: it adds presentation, derived
// data, and whatever local scaffolding this machine needs, and it must not
// touch the replicated values themselves -- the next snapshot owns those.
//
// Runs where structural mutation is legal, which is the snapshot apply point.
//-----------------------------------------------------------------------------
class NetSpawnRecipes
{
public:
    using Builder = std::function<void(World&, EntityId)>;

    // Ids are the game's to choose and must match on both ends, the same way
    // the replicated component table must.
    //
    // False for an id that is already registered, for a null builder, and for
    // zero, which names the absence of a recipe rather than one of them. A
    // second claim on an id is refused rather than allowed to replace the
    // first: two features that both picked seven would otherwise produce a
    // build where one silently wears the other's presentation, and the symptom
    // -- an entity whose state is right and whose appearance is not -- is
    // nowhere near the registration that caused it.
    //
    // A module reload re-registers from Clear(), so replacement never needed to
    // be the way an id changes hands.
    [[nodiscard]] bool Register(NetSpawnRecipeId id, Builder build);
    void Clear() { Builders.clear(); }

    // Runs the recipe for `id`, if one is registered. A name this build does
    // not know is not an error: an authority may legitimately run content this
    // client did not register, and the entity keeps its replicated state.
    // Returns whether anything ran, so a caller can report the gap once.
    bool Build(NetSpawnRecipeId id, World& world, EntityId entity) const;

    [[nodiscard]] bool Contains(NetSpawnRecipeId id) const;
    [[nodiscard]] std::size_t Size() const { return Builders.size(); }

private:
    std::unordered_map<NetSpawnRecipeId, Builder> Builders;
};
