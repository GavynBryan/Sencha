#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/BinaryArchive.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/World.h>
#include <logic/ResponseSet.h>
#include <logic/Responses.h>
#include <logic/ResponsesSerializer.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <memory>
#include <sstream>
#include <string>

// Responses' scene form: the bindings exactly as written -- including an event
// and a verb this World has never heard of -- and no runtime state.

namespace
{
constexpr std::string_view kAuthored = R"({"bindings": {
    "walk": {"on": "sensor.stay", "enabled": false, "once": true, "cooldown": 1.5, "cooldown_scope": "instigator",
             "sequence": {"conditions": [{"entity": "activator", "has": ["sencha.character_movement"]}],
                          "commands": [{"verb": "game.not_declared", "arguments": {"target": {"port": "activator"}},
                                        "on_failure": "stop"}]}},
    "shot": {"on": "game.not_declared", "sequence": {"commands": []}}}})";

class ResponsesSerializerFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Serializer = MakeResponsesSerializer();
        Serializer->RegisterStorage(Scene);
    }

    [[nodiscard]] bool LoadFrom(const JsonValue& json, EntityId entity)
    {
        JsonReadArchive in{ json };
        SceneSerializationContext context{ Logging };
        return Serializer->Load(in, entity, Scene, context);
    }

    [[nodiscard]] JsonValue SaveOf(EntityId entity)
    {
        JsonWriteArchive out;
        SceneSerializationContext context{ Logging };
        EXPECT_TRUE(Serializer->Save(out, entity, Scene, context));
        return out.TakeValue();
    }

    LoggingProvider Logging;
    Registry Scene;
    std::unique_ptr<IComponentSerializer> Serializer;
};
} // namespace

TEST_F(ResponsesSerializerFixture, TheBindingsRoundTripVerbatimAndLoadAsAuthored)
{
    const JsonValue authored = *JsonParse(kAuthored);
    const EntityId entity = Scene.Components.CreateEntity();
    ASSERT_TRUE(LoadFrom(authored, entity));

    const Responses* responses = Scene.Components.TryGet<Responses>(entity);
    ASSERT_NE(responses, nullptr);
    const std::span<const ResponseBindingDesc> bindings =
        ResponseDefinitionsOf(Scene.Components).Sets.Bindings(responses->Set);
    ASSERT_EQ(bindings.size(), 2u);
    EXPECT_EQ(bindings[0].Key, "shot") << "bindings are held in key order";
    const ResponseBindingDesc& walk = bindings[1];
    EXPECT_EQ(walk.On, "sensor.stay");
    EXPECT_FALSE(walk.StartsEnabled);
    EXPECT_TRUE(walk.Once);
    EXPECT_FLOAT_EQ(walk.CooldownSeconds, 1.5f);
    EXPECT_EQ(walk.CooldownScope, ResponseCooldownScope::Instigator);
    EXPECT_EQ(responses->Disabled, 1u << 1) << "a binding authored disabled did not load disabled";
    EXPECT_EQ(responses->Consumed, 0u);

    const JsonValue saved = SaveOf(entity);
    ASSERT_NE(saved.Find("bindings"), nullptr);
    EXPECT_EQ(JsonStringify(*saved.Find("bindings")), JsonStringify(*authored.Find("bindings")));
}

TEST_F(ResponsesSerializerFixture, RuntimeStateIsNotSceneData)
{
    const EntityId entity = Scene.Components.CreateEntity();
    ASSERT_TRUE(LoadFrom(*JsonParse(kAuthored), entity));
    Scene.Components.TryGet<Responses>(entity)->Consumed = 1u;
    Scene.Components.TryGet<Responses>(entity)->Disabled = 0u;

    const EntityId copy = Scene.Components.CreateEntity();
    ASSERT_TRUE(LoadFrom(SaveOf(entity), copy));
    EXPECT_EQ(Scene.Components.TryGet<Responses>(copy)->Consumed, 0u);
    EXPECT_EQ(Scene.Components.TryGet<Responses>(copy)->Disabled, 1u << 1);
}

TEST_F(ResponsesSerializerFixture, MissingBindingsLoadAsNone)
{
    const EntityId entity = Scene.Components.CreateEntity();
    ASSERT_TRUE(LoadFrom(*JsonParse("{}"), entity));
    const Responses* responses = Scene.Components.TryGet<Responses>(entity);
    ASSERT_NE(responses, nullptr);
    EXPECT_TRUE(ResponseDefinitionsOf(Scene.Components).Sets.Bindings(responses->Set).empty());
    const JsonValue* bindings = ResponseBindingsSource(Scene.Components, entity);
    ASSERT_NE(bindings, nullptr);
    EXPECT_EQ(JsonStringify(*bindings), JsonStringify(EmptyResponseBindings()));
}

TEST_F(ResponsesSerializerFixture, MalformedBindingsAreReportedAndTheRestStillLoad)
{
    const EntityId entity = Scene.Components.CreateEntity();
    ASSERT_TRUE(LoadFrom(*JsonParse(R"({"bindings": {
        "bad key": {"on": "sensor.enter"},
        "no_event": {"sequence": {"commands": []}},
        "fine": {"on": "sensor.enter", "cooldown": -1}}})"),
                         entity));
    const ResponseDefinitions& definitions = ResponseDefinitionsOf(Scene.Components);
    const ResponseSetKey set = Scene.Components.TryGet<Responses>(entity)->Set;
    EXPECT_EQ(definitions.Sets.Bindings(set).size(), 2u) << "a key that is not a stable key was kept";
    EXPECT_EQ(definitions.Sets.Diagnostics(set).size(), 3u)
        << "the bad key, the missing event and the negative cooldown are each reported";
}

TEST_F(ResponsesSerializerFixture, TheBinaryArchiveCarriesTheBindingsToo)
{
    const EntityId entity = Scene.Components.CreateEntity();
    ASSERT_TRUE(LoadFrom(*JsonParse(kAuthored), entity));

    std::stringstream bytes;
    {
        BinaryWriter writer(bytes);
        BinaryWriteArchive out(writer);
        SceneSerializationContext context{ Logging };
        ASSERT_TRUE(Serializer->Save(out, entity, Scene, context));
    }
    const EntityId copy = Scene.Components.CreateEntity();
    BinaryReader reader(bytes);
    BinaryReadArchive in(reader);
    SceneSerializationContext context{ Logging };
    ASSERT_TRUE(Serializer->Load(in, copy, Scene, context));
    EXPECT_EQ(Scene.Components.TryGet<Responses>(copy)->Set, Scene.Components.TryGet<Responses>(entity)->Set)
        << "the same bindings did not intern to the same set";
}
