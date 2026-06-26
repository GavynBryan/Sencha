// A minimal, real game module for the S2 boundary integration test. Built as a
// loadable library (MODULE) against the shared engine ABI, with hidden default
// visibility; only SenchaCreateGameModule is exported. It is a Game (the v4
// module contract) whose OnRegisterComponents registers a game-defined component
// serializer into the host-owned registry and OnUnregisterComponents retracts it
// — the in-tree analog of a shipped game.so, exercised without running the game.

#include <app/Game.h>
#include <app/GameModule.h>
#include <core/metadata/Field.h>
#include <core/serialization/FourCC.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/ComponentStorageTraits.h>

#include <memory>
#include <string_view>
#include <tuple>

// A purely game-defined component — the engine has never heard of it.
struct GrappleHook
{
    double AnchorX = 0.0;
    double AnchorY = 0.0;
    double AnchorZ = 0.0;
    float  Length  = 0.0f;
};

template <>
struct TypeSchema<GrappleHook>
{
    static constexpr std::string_view Name        = "spike.grapple_hook";
    static constexpr std::uint32_t    SceneChunkId = MakeFourCC('G', 'R', 'A', 'P');

    static auto Fields()
    {
        return std::tuple{
            MakeField("anchor_x", &GrappleHook::AnchorX),
            MakeField("anchor_y", &GrappleHook::AnchorY),
            MakeField("anchor_z", &GrappleHook::AnchorZ),
            MakeField("length",   &GrappleHook::Length),
        };
    }
};

namespace
{
    struct TestGameModule final : Game
    {
        void OnRegisterComponents(ComponentSerializerRegistry& serializers) override
        {
            serializers.Register(std::make_unique<ComponentSerializer<GrappleHook>>());
        }

        void OnUnregisterComponents(ComponentSerializerRegistry& serializers) override
        {
            // Module-owns: retract exactly our serializer while still mapped.
            serializers.Remove(ResolveComponentTypeId<GrappleHook>());
        }
    };
}

extern "C" SENCHA_GAME_EXPORT Game* SenchaCreateGameModule()
{
    // Module-owned static instance: nothing for the host to delete across the
    // allocator boundary; teardown is OnUnregisterComponents + unmap.
    static TestGameModule instance;
    return &instance;
}

// The C-linkage ABI descriptor the loader validates before touching the vtable.
SENCHA_EXPORT_GAME_MODULE_ABI()
