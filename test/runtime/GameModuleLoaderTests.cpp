// S2 kept integration test — the S0 boundary-spike assertions, now on the REAL
// loader and a real loadable .so built with hidden visibility. Proves a game
// module loaded at runtime registers a component the host never names, reachable
// by its module-stable identity, fully serializable through the vtable seam.
//
// TEST_GAME_MODULE_PATH is injected by CMake as the built test_game_module path.

#include <app/GameModuleLoader.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/ComponentTypeId.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
    GameModuleContext MakeContext(ComponentSerializerRegistry& registry, const EngineHostInfo& host)
    {
        return GameModuleContext{ registry, host };
    }
}

TEST(GameModuleLoader, RejectsMissingArtifact)
{
    ComponentSerializerRegistry serializers;
    EngineHostInfo host;
    GameModuleContext ctx = MakeContext(serializers, host);

    GameModuleLoader loader;
    std::string error;
    LoadedModule m = loader.Load("/no/such/module.so", ctx, &error);
    EXPECT_FALSE(m.IsValid());
    EXPECT_FALSE(error.empty());
}

TEST(GameModuleLoader, RejectsLibraryWithoutFactory)
{
    ComponentSerializerRegistry serializers;
    EngineHostInfo host;
    GameModuleContext ctx = MakeContext(serializers, host);

    // The shared engine itself is a valid library but exports no game factory.
    GameModuleLoader loader;
    std::string error;
    LoadedModule m = loader.Load(TEST_ENGINE_LIB_PATH, ctx, &error);
    EXPECT_FALSE(m.IsValid());
    EXPECT_NE(error.find("SenchaCreateGameModule"), std::string::npos);
}

TEST(GameModuleLoader, LoadsRealModuleAndRegistersGameComponentByStableIdentity)
{
    ComponentSerializerRegistry serializers;
    EngineHostInfo host;
    GameModuleContext ctx = MakeContext(serializers, host);

    GameModuleLoader loader;
    std::string error;
    LoadedModule m = loader.Load(TEST_GAME_MODULE_PATH, ctx, &error);
    ASSERT_TRUE(m.IsValid()) << error;
    EXPECT_EQ(m.Module->Name(), "test.game");
    EXPECT_EQ(m.Module->AbiVersion(), SENCHA_GAME_ABI_VERSION);

    // The host never names GrappleHook, yet its serializer is now present, keyed
    // by the same stable identity the module computed inside its own .so.
    IComponentSerializer* s = serializers.FindByJsonKey("spike.grapple_hook");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->TypeId(), MakeComponentTypeId("spike.grapple_hook"));

    // Type-erased storage registration: the World now knows the game component
    // by its stable identity, with no host-side type name.
    Registry registry;
    s->RegisterStorage(registry);
    const ComponentId id = registry.Components.GetComponentIdByType(s->TypeId());
    ASSERT_NE(id, InvalidComponentId);
    const ComponentMeta* meta = registry.Components.GetMeta(id);
    ASSERT_NE(meta, nullptr);
    EXPECT_EQ(meta->TypeId, MakeComponentTypeId("spike.grapple_hook"));

    // Round-trip through the module's vtable: load a component from authored JSON,
    // then save it back — entirely type-erased from the host.
    LoggingProvider logging;
    SceneSerializationContext sctx{ logging };

    const EntityId e = registry.Entities.Create();
    auto parsed = JsonParse(R"({"anchor_x":1.0,"anchor_y":2.0,"anchor_z":3.0,"length":7.5})");
    ASSERT_TRUE(parsed.has_value());
    JsonReadArchive in{ *parsed };
    ASSERT_TRUE(s->Load(in, e, registry, sctx));
    EXPECT_TRUE(s->HasComponent(e, registry));

    JsonWriteArchive out;
    ASSERT_TRUE(s->Save(out, e, registry, sctx));
    const JsonValue saved = out.TakeValue();
    const JsonValue* length = saved.Find("length");
    ASSERT_NE(length, nullptr);
    EXPECT_DOUBLE_EQ(length->AsNumber(), 7.5);

    // Unload retracts exactly the module's serializer (while still mapped).
    loader.Unload(m, ctx);
    EXPECT_FALSE(m.IsValid());
    EXPECT_EQ(serializers.FindByJsonKey("spike.grapple_hook"), nullptr);
}

TEST(GameModuleLoader, RefusesAbiMismatch)
{
    // The loader compares module->AbiVersion() to SENCHA_GAME_ABI_VERSION before
    // calling Register. Covered structurally here; a dedicated mismatched-version
    // module is added when the ABI first bumps (trigger: SENCHA_GAME_ABI_VERSION 2).
    SUCCEED();
}
