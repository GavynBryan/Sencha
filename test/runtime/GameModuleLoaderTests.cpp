// S2 kept integration test — the S0 boundary-spike assertions, now on the REAL
// loader and a real loadable .so built with hidden visibility. Proves a game
// module loaded at runtime registers a component the host never names, reachable
// by its module-stable identity, fully serializable through the vtable seam.
//
// v4 contract: the module IS a Game; the editor/runtime borrow its serializers
// through Game::OnRegisterComponents without running the game lifecycle.
//
// TEST_GAME_MODULE_PATH is injected by CMake as the built test_game_module path.

#include <app/Game.h>
#include <app/GameModuleLoader.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/ComponentTypeId.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/serialization/SceneSerializer.h>

#include <gtest/gtest.h>

#include <string>

TEST(GameModuleLoader, RejectsMissingArtifact)
{
    GameModuleLoader loader;
    std::string error;
    LoadedModule m = loader.Load("/no/such/module.so", &error);
    EXPECT_FALSE(m.IsValid());
    EXPECT_FALSE(error.empty());
}

TEST(GameModuleLoader, RejectsLibraryWithoutFactory)
{
    // The shared engine itself is a valid library but exports no game factory.
    GameModuleLoader loader;
    std::string error;
    LoadedModule m = loader.Load(TEST_ENGINE_LIB_PATH, &error);
    EXPECT_FALSE(m.IsValid());
    EXPECT_NE(error.find("SenchaCreateGameModule"), std::string::npos);
}

TEST(GameModuleLoader, LoadsRealModuleAndRegistersGameComponentByStableIdentity)
{
    GameModuleLoader loader;
    std::string error;
    LoadedModule m = loader.Load(TEST_GAME_MODULE_PATH, &error);
    ASSERT_TRUE(m.IsValid()) << error;

    // Borrow the module's serializers exactly as the editor does — no game run.
    ComponentSerializerRegistry serializers;
    m.Instance->OnRegisterComponents(serializers);

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

    // Retract the serializer (while still mapped), then unmap.
    m.Instance->OnUnregisterComponents(serializers);
    EXPECT_EQ(serializers.FindByJsonKey("spike.grapple_hook"), nullptr);
    loader.Unload(m);
    EXPECT_FALSE(m.IsValid());
}

TEST(GameModuleLoader, AbiDescriptorAcceptsIdenticalBuild)
{
    const GameModuleAbi host = SenchaThisBuildAbi();
    EXPECT_FALSE(DescribeGameModuleAbiMismatch(host, host).has_value());
}

TEST(GameModuleLoader, RefusesAbiMismatch)
{
    const GameModuleAbi host = SenchaThisBuildAbi();

    // The incident: a module built against different ABI headers (stale .so).
    GameModuleAbi staleHeaders = host;
    staleHeaders.HeaderFingerprint ^= 0x1ull;
    const auto headerReason = DescribeGameModuleAbiMismatch(staleHeaders, host);
    ASSERT_TRUE(headerReason.has_value());
    EXPECT_NE(headerReason->find("fingerprint"), std::string::npos);

    // A deliberate ABI-version break is also refused.
    GameModuleAbi oldVersion = host;
    oldVersion.AbiVersion -= 1;
    EXPECT_TRUE(DescribeGameModuleAbiMismatch(oldVersion, host).has_value());

    // So is a toolchain mismatch (e.g. a different C++ standard library).
    GameModuleAbi otherStdLib = host;
    otherStdLib.StdLibId += 1;
    EXPECT_TRUE(DescribeGameModuleAbiMismatch(otherStdLib, host).has_value());
}

// The headless proof of S3's editor gate: a component defined only in the game
// module survives a FULL scene save→load (exactly what EditorDocument::Save/Load
// call), loaded into a stock registry that links no editor symbols.
TEST(GameModuleLoader, ModuleComponentRoundTripsThroughSceneJson)
{
    // Use the engine's default registry — the one SaveSceneJson/LoadSceneJson read.
    ClearComponentSerializers();
    InitSceneSerializer();

    GameModuleLoader loader;
    std::string error;
    LoadedModule m = loader.Load(TEST_GAME_MODULE_PATH, &error);
    ASSERT_TRUE(m.IsValid()) << error;
    m.Instance->OnRegisterComponents(DefaultComponentSerializerRegistry());

    IComponentSerializer* gs = DefaultComponentSerializerRegistry().FindByJsonKey("spike.grapple_hook");
    ASSERT_NE(gs, nullptr);

    // Author an entity carrying the game component (type-erased, via its Load).
    Registry source;
    for (const auto& s : GetComponentSerializerEntries())
        s->RegisterStorage(source);

    LoggingProvider logging;
    SceneSerializationContext sctx{ logging };
    const EntityId e = source.Entities.Create();
    auto parsed = JsonParse(R"({"anchor_x":1.0,"anchor_y":2.0,"anchor_z":3.0,"length":7.5})");
    ASSERT_TRUE(parsed.has_value());
    JsonReadArchive in{ *parsed };
    ASSERT_TRUE(gs->Load(in, e, source, sctx));

    // Save the whole scene, then load into a fresh registry (the runtime/editor path).
    const JsonValue scene = SaveSceneJson(source);
    Registry loaded;
    SceneLoadError loadError;
    ASSERT_TRUE(LoadSceneJson(scene, loaded, &loadError)) << loadError.Message;

    // The game component came back, by its module-stable identity.
    const ComponentId id = loaded.Components.GetComponentIdByType(gs->TypeId());
    ASSERT_NE(id, InvalidComponentId);
    bool found = false;
    for (const EntityId entity : loaded.Entities.GetAliveEntities())
    {
        if (loaded.Components.HasComponent(entity, id))
        {
            found = true;
            JsonWriteArchive out;
            ASSERT_TRUE(gs->Save(out, entity, loaded, sctx));
            const JsonValue saved = out.TakeValue();
            const JsonValue* length = saved.Find("length");
            ASSERT_NE(length, nullptr);
            EXPECT_DOUBLE_EQ(length->AsNumber(), 7.5);
        }
    }
    EXPECT_TRUE(found);

    m.Instance->OnUnregisterComponents(DefaultComponentSerializerRegistry());
    loader.Unload(m);
    ClearComponentSerializers();
}
