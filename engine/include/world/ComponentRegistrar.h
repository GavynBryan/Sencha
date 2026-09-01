#pragma once

#include <core/assets/AssetRef.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/WorldComponentSchema.h>
#include <net/ReplicationLayout.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>

#include <cassert>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

//=============================================================================
// ComponentRegistrar
//
// One place a component is declared to exist, and one place that decides what
// follows from it.
//
// A component type has at most three consequences: it needs storage in the
// world vocabulary, it may be saved into a scene, and it may travel to a peer.
// Those are three separate registries, and keeping them in step used to mean
// naming the same component in three lists that nothing forced to agree. The
// component's own schema already states the two policy facts -- SceneChunkId
// means it persists, Replicated means it travels -- so this reads them and
// fans one Add out to whichever registries the host owns.
//
// The consequence worth stating plainly: adding a component is editing the
// component's own file and one Add line in its feature's registrar. Nothing
// central grows, and a server-authoritative gameplay component costs a game
// module exactly the same two edits, with no engine change at all.
//
// A host supplies the registries it has. The editor authors scenes without a
// live World or a session, so it supplies serializers alone; the runtime
// supplies all three; a test supplies what it is testing.
//
// Storage comes in two forms because there are two ways a World gets its
// vocabulary: the runtime composes a schema, seals it, and applies it to every
// World it makes, while the editor's preview registry and hand-built test
// Worlds register into one World directly. Both walk the same feature
// registrars, so neither can drift from the other.
//=============================================================================

// The component persists into scenes. Declared by the chunk id its schema
// carries, which is also the on-disk key -- so a component that can be saved
// says so exactly once, in the place that says how.
template <typename T>
inline constexpr bool ComponentIsSceneSerialized = requires { TypeSchema<T>::SceneChunkId; };

// The component's values travel from an authority to its peers. Declared as
// `static constexpr bool Replicated = true;` on the schema, beside the
// per-field OwnerOnly/LocalOnly/Quantize annotations that only mean anything
// when it is set.
template <typename T>
inline constexpr bool ComponentIsReplicated = []
{
    if constexpr (requires { TypeSchema<T>::Replicated; })
        return static_cast<bool>(TypeSchema<T>::Replicated);
    else
        return false;
}();

// Whether every default the schema states matches the member initializer it
// describes. They have to agree: a decoder that leaves an unmentioned field
// alone writes the member initializer underneath it, while a writer that omits
// a field does so because it equals the schema default -- so a disagreement is
// a field that saves as one value and loads as another. Checked at
// registration, where the answer is the same for every component and nothing
// has to remember to ask.
template <typename T>
[[nodiscard]] bool ComponentDefaultsMatchInitializers()
{
    const T initialized{};
    bool ok = true;
    std::apply(
        [&](const auto&... field)
        {
            ([&]
             {
                 if (!field.DefaultValue.has_value())
                     return;
                 if (!(initialized.*field.Ptr == *field.DefaultValue))
                     ok = false;
             }(),
             ...);
        },
        TypeSchema<T>::Fields());
    return ok;
}

// Whether any of T's fields is an asset reference. A component that names an
// asset has to own it: the scene load hands its reference over and lets go, so
// the only thing keeping the asset alive afterwards is the component's own
// OnAdd, and the only thing that frees it is the matching OnRemove.
template <typename T>
[[nodiscard]] bool ComponentNamesAnAsset()
{
    bool any = false;
    std::apply([&](const auto&... field)
               { ((any = any || field.Asset != AssetType::Unknown), ...); },
               TypeSchema<T>::Fields());
    return any;
}

class ComponentRegistrar
{
public:
    ComponentRegistrar(WorldComponentSchema* storage,
                       ComponentSerializerRegistry* serializers,
                       ReplicationLayout* replication)
        : Storage(storage)
        , Serializers(serializers)
        , Replication(replication)
    {
    }

    explicit ComponentRegistrar(World& world)
        : DirectWorld(&world)
    {
    }

    // Registers T everywhere its schema says it belongs. Idempotent in every
    // registry, so a component two features both depend on can be named by
    // both without either having to know.
    template <typename T>
    void Add()
    {
        // Whether a component is serialized is read off its schema, so a unit
        // that cannot see the schema registers it as a component that is not --
        // storage without a serializer, and every scene naming it stops round
        // tripping. The component's header states that its feature has one.
        static_assert(!ComponentDeclaresSchema<T> || HasTypeSchema<T>,
                      "This component declares a schema, but none is visible "
                      "here: include the feature schema unit that defines it. "
                      "Registering without it silently drops the component's "
                      "serializer and its place in the replication table.");

        // The predicted set is a subset of the wire table: a client resumes
        // simulating a component from what the authority said about it, and a
        // component that does not travel has nothing to resume from.
        static_assert(!ComponentIsPredicted<T> || ComponentIsReplicated<T>,
                      "A predicted component must also be replicated: prediction "
                      "resumes from the authority's value, which only arrives if "
                      "the component travels.");

        if constexpr (ComponentIsSceneSerialized<T> || ComponentIsReplicated<T>)
        {
            assert(ComponentDefaultsMatchInitializers<T>()
                   && "A schema default differs from the member initializer it "
                      "describes: the component would save as one value and "
                      "load as another.");
        }

        if constexpr (ComponentIsSceneSerialized<T>)
        {
            assert((!ComponentNamesAnAsset<T>()
                    || (ComponentHasOnAdd<T> && ComponentHasOnRemove<T>))
                   && "A component names an asset but has no ComponentTraits to "
                      "retain and release it: loading one from a scene would "
                      "leave the component holding an asset nothing owns.");
        }

        if (Storage != nullptr)
            Storage->Add<T>();

        if (DirectWorld != nullptr && !DirectWorld->IsRegistered<T>())
            DirectWorld->RegisterComponent<T>();

        if constexpr (ComponentIsSceneSerialized<T>)
        {
            if (Serializers != nullptr
                && Serializers->FindByType(ResolveComponentTypeId<T>()) == nullptr)
            {
                RegisterComponent<T>(*Serializers);
                Added_.push_back(ResolveComponentTypeId<T>());
            }
        }

        if constexpr (ComponentIsReplicated<T>)
        {
            if (Replication != nullptr)
                Replication->Add<T>();
        }
    }

    // Registers a persisted form the component's own TypeSchema cannot state.
    //
    // A few components persist as names rather than as values: a tag container
    // saves the tags' registered names and an attribute set saves its
    // attributes', because the runtime ids are registration-order values that
    // mean nothing in another process or another build. Those components carry
    // a hand-written serializer instead of a schema, and this is where it
    // enters -- beside the Add<T> that gives the component its storage, so the
    // two facts about one component are stated in one place.
    //
    // Registering the same serializer twice is a no-op, so a component two
    // features both name costs nothing. A different serializer claiming an
    // identity another component already holds -- its type, its json key, or
    // its chunk id -- would make one component load as another, so it is
    // refused rather than quietly dropped and returns false.
    bool AddSerializer(std::unique_ptr<IComponentSerializer> serializer)
    {
        assert(serializer != nullptr
               && "AddSerializer needs a serializer to register.");
        if (serializer == nullptr)
            return false;

        // A host that owns no serializer registry (a bare World, a schema-only
        // composition) is owed nothing, exactly as it is for Add<T>.
        if (Serializers == nullptr)
            return true;

        const ComponentTypeId type = serializer->TypeId();
        const ComponentSerializerRegistry::RegisterResult result =
            Serializers->Register(std::move(serializer));

        assert(result != ComponentSerializerRegistry::RegisterResult::Rejected
               && "A serializer claims a component identity another component "
                  "already holds: one of them would load as the other.");

        if (result == ComponentSerializerRegistry::RegisterResult::Added)
            Added_.push_back(type);
        return result != ComponentSerializerRegistry::RegisterResult::Rejected;
    }

    // The serializers this registrar added, in the order it added them. A host
    // that loads a game module retracts exactly these when unloading it: the
    // serializer objects are constructed by whichever binary called Add, and
    // freeing them after that binary is unmapped would run a vanished
    // destructor. Recording it here is what lets a game module declare its
    // components once instead of listing them again to take them back.
    [[nodiscard]] std::span<const ComponentTypeId> AddedSerializers() const
    {
        return { Added_.data(), Added_.size() };
    }

private:
    WorldComponentSchema* Storage = nullptr;
    World* DirectWorld = nullptr;
    ComponentSerializerRegistry* Serializers = nullptr;
    ReplicationLayout* Replication = nullptr;

    std::vector<ComponentTypeId> Added_;
};
