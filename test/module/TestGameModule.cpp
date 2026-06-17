// A minimal, real game module for the S2 boundary integration test. Built as a
// loadable library (MODULE) against the shared engine ABI, with hidden default
// visibility; only SenchaCreateGameModule is exported. It defines a game
// component, registers its serializer into the host-owned registry, and retracts
// it on unregister — the in-tree analog of a shipped game.so.

#include <app/GameModule.h>
#include <core/console/ConsoleRegistry.h>
#include <core/metadata/Field.h>
#include <core/serialization/FourCC.h>
#include <world/serialization/ComponentSerializer.h>
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
    struct TestGameModule final : IGameModule
    {
        std::string_view Name() const override { return "test.game"; }
        std::uint32_t    AbiVersion() const override { return SENCHA_GAME_ABI_VERSION; }

        void Register(GameModuleContext& ctx) override
        {
            ctx.Serializers.Register(std::make_unique<ComponentSerializer<GrappleHook>>());
            ConsoleResult cvarResult;
            (void)cvarResult;
            ctx.Console.RegisterCVar({
                .Name = "test.grapple_length",
                .Owner = "test.game",
                .Type = CVarType::Double,
                .DefaultValue = 7.5,
                .CurrentValue = 7.5,
                .Help = "Test module grapple length.",
            }, &cvarResult);
            ConsoleResult commandResult;
            (void)commandResult;
            ctx.Console.RegisterCommand({
                .Name = "test.grapple",
                .Owner = "test.game",
                .Usage = "test.grapple",
                .Help = "Test module command.",
                .Callback = [](ConsoleExecutionContext&, std::span<const std::string>) {
                    ConsoleResult result;
                    result.Info("grapple");
                    return result;
                },
            }, &commandResult);
        }

        void Unregister(GameModuleContext& ctx) override
        {
            // Module-owns: retract exactly our serializer while still mapped.
            ctx.Serializers.Remove(ResolveComponentTypeId<GrappleHook>());
            ctx.Console.UnregisterOwner("test.game");
        }
    };
}

extern "C" SENCHA_GAME_EXPORT IGameModule* SenchaCreateGameModule()
{
    // Module-owned static instance: nothing for the host to delete across the
    // allocator boundary; teardown is Unregister + unmap.
    static TestGameModule instance;
    return &instance;
}

// The C-linkage ABI descriptor the loader validates before touching the vtable.
SENCHA_EXPORT_GAME_MODULE_ABI()
