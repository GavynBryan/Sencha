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
        EXPECT_TRUE(Serializer->Save(archive, Actor, Scene, Context));
        return archive.TakeValue();
    }

    [[nodiscard]] const AbilityGrants& Grants() { return *Scene.Components.TryGet<AbilityGrants>(Actor); }
    [[nodiscard]] const AbilityGrantEntry& Entry(std::size_t i)
    {
        return *Scene.Components.GetResource<AbilityGrantEntries>().Find(Grants().Entries[i].Entry);
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
    EXPECT_EQ(abilities.GetName(first.Grants().Entries[0].Ability), "asset://abilities/charge.sdata")
        << "named before anything declared it";

    GrantsScene second;
    ASSERT_TRUE(second.Load(first.Save()));
    ASSERT_EQ(second.Grants().Count, 2u);
    EXPECT_EQ(second.Entry(0).Key, first.Entry(0).Key);
    EXPECT_TRUE(second.Entry(0).Override == first.Entry(0).Override);
    EXPECT_EQ(second.Entry(1).Key, "g_plain");
}

TEST(AbilityGrantsSerializer, OnlyAuthoredGrantsAreSaved)
{
    GrantsScene scene;
    ASSERT_TRUE(scene.Load(*JsonParse(kGrants)));
    AbilityGrants& grants = *scene.Scene.Components.TryGet<AbilityGrants>(scene.Actor);
    const std::uint16_t entry = scene.Scene.Components.GetResource<AbilityGrantEntries>().Intern(
        { "scripted", grants.Entries[1].Ability, {} });
    ASSERT_TRUE(GrantAbility(grants, grants.Entries[1].Ability, AbilityGrantKind::Request, entry).IsValid());

    const JsonValue saved = scene.Save();
    ASSERT_NE(saved.Find("grants"), nullptr);
    EXPECT_EQ(saved.Find("grants")->AsArray().size(), 2u);
}

TEST(AbilityGrantsSerializer, AnUnknownBindingChoiceRefusesTheScene)
{
    GrantsScene scene;
    EXPECT_FALSE(scene.Load(*JsonParse(R"({ "grants": [ { "key": "g", "ability": "asset://abilities/a.sdata",
        "binding": { "policy": "sometimes" } } ] })")));
}
