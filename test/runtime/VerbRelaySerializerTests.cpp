#include <gtest/gtest.h>

#include <assets/data/DataAssetCache.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/World.h>
#include <logic/VerbRelay.h>
#include <logic/VerbRelaySerializer.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <memory>
#include <string>

// The relay's persisted key is the text the author wrote, not the number the
// component holds. A key nothing could spell falls back to the hash's digits,
// and either form loads to the same component.

namespace
{
class RelaySerializerFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Serializer = MakeVerbRelaySerializer();
        Serializer->RegisterStorage(Scene);
    }

    [[nodiscard]] bool LoadFrom(std::string_view json, EntityId entity)
    {
        const auto parsed = JsonParse(std::string(json));
        if (!parsed.has_value())
            return false;
        JsonReadArchive in{ *parsed };
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
}

TEST_F(RelaySerializerFixture, TheKeyTextRoundTripsAndLoadsToItsHash)
{
    const EntityId entity = Scene.Components.CreateEntity();
    ASSERT_TRUE(LoadFrom(R"({"binding":"arena.relay_award"})", entity));

    const VerbRelay* relay = Scene.Components.TryGet<VerbRelay>(entity);
    ASSERT_NE(relay, nullptr);
    EXPECT_EQ(relay->Binding, MakeVerbBindingKey("arena.relay_award"));

    const JsonValue saved = SaveOf(entity);
    const JsonValue* key = saved.Find("binding");
    ASSERT_NE(key, nullptr);
    ASSERT_TRUE(key->IsString());
    // What the author wrote, not what the component holds.
    EXPECT_EQ(key->AsString(), "arena.relay_award");
}

TEST_F(RelaySerializerFixture, ADigitsFormLoadsToTheSameHashAndSavesAsDigits)
{
    const EntityId entity = Scene.Components.CreateEntity();
    const std::string digits = VerbBindingKeyToString(MakeVerbBindingKey("arena.relay_award"));
    ASSERT_TRUE(LoadFrom(R"({"binding":")" + digits + R"("})", entity));

    const VerbRelay* relay = Scene.Components.TryGet<VerbRelay>(entity);
    ASSERT_NE(relay, nullptr);
    EXPECT_EQ(relay->Binding, MakeVerbBindingKey("arena.relay_award"));

    // Nothing in this World has spelled the key, so the digits are the only
    // truth left, and they are what goes back out.
    const JsonValue saved = SaveOf(entity);
    ASSERT_NE(saved.Find("binding"), nullptr);
    EXPECT_EQ(saved.Find("binding")->AsString(), digits);
}

TEST_F(RelaySerializerFixture, ASpellingLearnedFromOneRelayServesAnotherWithTheSameKey)
{
    const EntityId spelled = Scene.Components.CreateEntity();
    ASSERT_TRUE(LoadFrom(R"({"binding":"door.open"})", spelled));

    const EntityId numbered = Scene.Components.CreateEntity();
    ASSERT_TRUE(LoadFrom(
        R"({"binding":")" + VerbBindingKeyToString(MakeVerbBindingKey("door.open")) + R"("})",
        numbered));

    const JsonValue saved = SaveOf(numbered);
    ASSERT_NE(saved.Find("binding"), nullptr);
    EXPECT_EQ(saved.Find("binding")->AsString(), "door.open");
}

TEST_F(RelaySerializerFixture, AnAbsentKeyLoadsAsNoBindingAndSavesNone)
{
    const EntityId entity = Scene.Components.CreateEntity();
    ASSERT_TRUE(LoadFrom(R"({})", entity));
    const VerbRelay* relay = Scene.Components.TryGet<VerbRelay>(entity);
    ASSERT_NE(relay, nullptr);
    EXPECT_FALSE(relay->Binding.IsValid());
    EXPECT_EQ(SaveOf(entity).Find("binding"), nullptr);
}
