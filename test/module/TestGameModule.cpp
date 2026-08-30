// A minimal, real game module for the S2 boundary integration test. Built as a
// loadable library (MODULE) against the shared engine ABI, with hidden default
// visibility; only SenchaCreateGameModule is exported. It is a Game (the v4
// module contract) whose OnRegisterComponents names a game-defined component and
// leaves the host to retract it -- the in-tree analog of a shipped game.so,
// exercised without running the game.

#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityRegistry.h>
#include <app/Game.h>
#include <app/GameModule.h>
#include <attributes/AttributeRegistry.h>
#include <core/metadata/Field.h>
#include <core/serialization/FourCC.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/LocomotionMode.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/ComponentRegistrar.h>
#include <world/serialization/ComponentStorageTraits.h>

#include <memory>
#include <string>
#include <string_view>
#include <tuple>

// The vocabulary this module declares, named here so a test can assert on the
// same strings the module registers.
inline constexpr std::string_view kGrappleTag = "Spike.Grappling";
inline constexpr std::string_view kGrappleAttribute = "GrappleRange";
inline constexpr std::string_view kGrappleAbility = "Grapple";
inline constexpr std::string_view kGrappleMode = "spike.mode.grapple";

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
        void OnRegisterComponents(ComponentRegistrar& registrar) override
        {
            registrar.Add<GrappleHook>();
        }

        // Names, not types: content refers to these as strings and resolves
        // them against whichever World it is loaded into, so every host that
        // owns a World installs them for itself.
        void OnRegisterVocabulary(World& world) override
        {
            if (auto* tags = world.TryGetResource<GameplayTagRegistry>())
                (void)tags->RegisterTag(kGrappleTag);
            if (auto* attributes = world.TryGetResource<AttributeRegistry>())
                (void)attributes->RegisterAttribute(kGrappleAttribute, 0.0f, 50.0f, 12.0f);
            if (auto* abilities = world.TryGetResource<AbilityRegistry>())
                (void)abilities->Register(kGrappleAbility, AbilityDefinition{});
            if (auto* modes = world.TryGetResource<LocomotionModeRegistry>();
                modes != nullptr && modes->Find(kGrappleMode) == nullptr)
            {
                (void)modes->Register<GrappleHook>(std::string(kGrappleMode));
            }
        }
    };
}

extern "C" SENCHA_GAME_EXPORT Game* SenchaCreateGameModule()
{
    // Module-owned static instance: nothing for the host to delete across the
    // allocator boundary; the host retracts what this registered, then unmaps.
    static TestGameModule instance;
    return &instance;
}

// The C-linkage ABI descriptor the loader validates before touching the vtable.
SENCHA_EXPORT_GAME_MODULE_ABI()
