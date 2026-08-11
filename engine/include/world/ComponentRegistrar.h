#pragma once

#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/WorldComponentSchema.h>
#include <net/ReplicationLayout.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>

#include <cassert>
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
