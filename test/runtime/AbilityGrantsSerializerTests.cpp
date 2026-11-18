// A scene's grants name abilities by asset and grants by stable key, and survive a
// save and a load with their binding overrides, whatever the loading World knows.

#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityGrantsSerializer.h>
#include <abilities/AbilityKit.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <gtest/gtest.h>

namespace
{
struct GrantsScene
{
    Registry Scene;
    LoggingProvider Logging;
    SceneSerializationContext Context{ Logging };
    std::unique_ptr<IComponentSerializer> Serializer = MakeAbilityGrantsSerializer();
    EntityId Actor;

    GrantsScene()
    {
        RegisterAbilityKit(Scene.Components);
        Actor = Scene.Components.CreateEntity();
    }

    bool Load(const JsonValue& json)
    {
        JsonReadArchive archive(json);
        return Serializer->LoadIntoWorld(archive, Actor, Scene.Components, Context);
    }

    JsonValue Save()
    {
        JsonWriteArchive archive;
        EXPECT_TRUE(Serializer->Save(archive, Actor, Scene.Components, Context));
        return archive.TakeValue();
    }

    [[nodiscard]] const AbilityGrants& Grants() { return *Scene.Components.TryGet<AbilityGrants>(Actor); }
    [[nodiscard]] std::span<const AbilityGrant> Held() { return AbilityGrantsOf(Scene.Components, Actor); }
    [[nodiscard]] const AbilityGrantEntry& Entry(std::size_t i)
    {
        return *Scene.Components.GetResource<AbilityGrantEntries>().Find(Held()[i].Entry);
    }
};

constexpr std::string_view kGrants = R"({ "grants": [
    { "key": "g_alt", "ability": "asset://abilities/charge.sdata",
      "binding": { "action": "Input.AltFire", "policy": "press_release", "priority": 50,
                   "context": { "none": ["Mode.Alt"] }, "on_interrupt": "release" } },
    { "key": "g_plain", "ability": "asset://abilities/primary.sdata" } ] })";
}

TEST(AbilityGrantsSerializer, AGrantKeepsItsKeyAbilityAndOverrideThroughASaveAndLoad)
{
    GrantsScene first;
    ASSERT_TRUE(first.Load(*JsonParse(kGrants)));
    ASSERT_EQ(first.Grants().Count, 2u);
    EXPECT_EQ(first.Entry(0).Key, "g_alt");
    EXPECT_EQ(first.Entry(0).Override.Action, "Input.AltFire");
    EXPECT_EQ(first.Entry(0).Override.Priority, 50);
    EXPECT_EQ(first.Entry(0).Override.OnInterrupt, AbilityInterrupt::Release);
    ASSERT_TRUE(first.Entry(0).Override.Context.has_value());
    EXPECT_EQ(first.Entry(0).Override.Context->None, std::vector<std::string>{ "Mode.Alt" });
    EXPECT_FALSE(first.Entry(1).Override.Action.has_value()) << "a grant without an override inherits everything";

    const AbilityRegistry& abilities = first.Scene.Components.GetResource<AbilityRegistry>();
    EXPECT_EQ(abilities.GetName(first.Held()[0].Ability), "asset://abilities/charge.sdata")
        << "named before anything declared it";

    GrantsScene second;
    ASSERT_TRUE(second.Load(first.Save()));
    ASSERT_EQ(second.Grants().Count, 2u);
    EXPECT_EQ(second.Entry(0).Key, first.Entry(0).Key);
    EXPECT_TRUE(second.Entry(0).Override == first.Entry(0).Override);
    EXPECT_EQ(second.Entry(1).Key, "g_plain");
}

TEST(AbilityGrantsSerializer, AGrantKeepsItsParametersThroughASaveAndLoad)
{
    GrantsScene first;
    ASSERT_TRUE(first.Load(*JsonParse(R"({ "grants": [
        { "key": "g_fireball", "ability": "asset://abilities/fireball.sdata", "parameters": [ { "key": "power", "value": 14 }, { "key": "arc", "value": 0.5 } ] } ] })")));
    ASSERT_EQ(first.Grants().Count, 1u);
    EXPECT_EQ(first.Entry(0).Parameters,
              (std::vector<AbilityGrantParameter>{ { "arc", 0.5 }, { "power", 14.0 } }))
        << "by name, whatever order they were written in";

    GrantsScene second;
    ASSERT_TRUE(second.Load(first.Save()));
    EXPECT_EQ(second.Entry(0).Parameters, first.Entry(0).Parameters);
}

TEST(AbilityGrantsSerializer, AuthoredAndRequestedGrantsAreSavedAndAnEffectsAreNot)
{
    GrantsScene scene;
    ASSERT_TRUE(scene.Load(*JsonParse(kGrants)));
    World& world = scene.Scene.Components;
    AbilityGrantEntries& entries = world.GetResource<AbilityGrantEntries>();
    const AbilityId ability = AbilityGrantsOf(world, scene.Actor)[1].Ability;
    const std::uint16_t requested = entries.Intern({ "requested", ability, {} });
    const std::uint16_t held = entries.Intern({ "held", ability, {} });
    ASSERT_TRUE(GrantAbility(world, scene.Actor, ability, AbilityGrantKind::Request, requested).IsValid());
    ASSERT_TRUE(GrantAbility(world, scene.Actor, ability, AbilityGrantKind::Effect, held).IsValid());

    const JsonValue saved = scene.Save();
    ASSERT_NE(saved.Find("grants"), nullptr);
    const JsonValue::Array& list = saved.Find("grants")->AsArray();
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[2].Find("key")->AsString(), "requested");
}

TEST(AbilityGrantsSerializer, FortyGrantsRoundTripInOrder)
{
    std::string json = R"({ "grants": [)";
    for (int i = 0; i < 40; ++i)
        json += std::string(i == 0 ? "" : ",") + R"({ "key": "g_)" + std::to_string(i)
            + R"(", "ability": "asset://abilities/a_)" + std::to_string(i % 7) + R"(.sdata" })";
    json += "] }";
    GrantsScene first;
    ASSERT_TRUE(first.Load(*JsonParse(json)));
    ASSERT_EQ(first.Grants().Count, 40u);

    GrantsScene second;
    ASSERT_TRUE(second.Load(first.Save()));
    ASSERT_EQ(second.Held().size(), 40u);
    const AbilityRegistry& abilities = second.Scene.Components.GetResource<AbilityRegistry>();
    for (std::size_t i = 0; i < 40; ++i)
    {
        EXPECT_EQ(second.Entry(i).Key, "g_" + std::to_string(i));
        EXPECT_EQ(abilities.GetName(second.Held()[i].Ability), "asset://abilities/a_" + std::to_string(i % 7) + ".sdata");
        EXPECT_EQ(second.Held()[i].Id.Value, i + 1);
    }
}

TEST(AbilityGrantsSerializer, AnUnknownBindingChoiceRefusesTheScene)
{
    GrantsScene scene;
    EXPECT_FALSE(scene.Load(*JsonParse(R"({ "grants": [ { "key": "g", "ability": "asset://abilities/a.sdata",
        "binding": { "policy": "sometimes" } } ] })")));
}
