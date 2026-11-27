#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <line/ContactLine.h>
#include <physics/components/HitRegion.h>
#include <projectile/Projectile.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/SceneSerializationContext.h>

#include <gtest/gtest.h>

#include <string>
#include <type_traits>

TEST(ProjectileTagFields, TagNamesAreHeldAsTagIds)
{
    static_assert(std::is_same_v<decltype(Projectile::PassesThrough), GameplayTagId>);
    static_assert(std::is_same_v<decltype(Projectile::HitKind), GameplayTagId>);
    static_assert(std::is_same_v<decltype(Projectile::ImpactCue), GameplayTagId>);
    static_assert(std::is_same_v<decltype(Projectile::Ignores), GameplayTagId>);
    static_assert(std::is_same_v<decltype(ContactLine::Kind), GameplayTagId>);
    static_assert(std::is_same_v<decltype(ContactLine::Ignores), GameplayTagId>);
    static_assert(std::is_same_v<decltype(ContactLine::AnchorCue), GameplayTagId>);
    static_assert(std::is_same_v<decltype(ContactLine::CutCue), GameplayTagId>);
    static_assert(std::is_same_v<decltype(HitRegion::Region), GameplayTagId>);
    EXPECT_EQ(sizeof(Projectile), 928u - 236u);
    EXPECT_EQ(sizeof(ContactLine), 480u - 240u);
    EXPECT_EQ(sizeof(HitRegion), 120u - 60u);
    EXPECT_EQ(sizeof(ContactLineState), 184u - 16u);
}

TEST(ProjectileTagFields, AProjectileNamingATagContentDoesNotDeclareIsRefusedAtLoad)
{
    World world;
    world.RegisterComponent<Projectile>();
    const GameplayTagId glass = *world.AddResource<GameplayTagRegistry>().RegisterTag("material.glass");
    LoggingProvider logging;
    SceneSerializationContext context(logging);
    ComponentSerializer<Projectile> serializer;

    const EntityId authored = world.CreateEntity();
    Projectile shot;
    shot.PassesThrough = glass;
    world.AddComponent<Projectile>(authored, shot);
    JsonWriteArchive written;
    ASSERT_TRUE(serializer.Save(written, authored, world, context));
    const std::string text = JsonStringify(written.TakeValue());
    ASSERT_NE(text.find(R"("passes_through":"material.glass")"), std::string::npos) << text;

    const EntityId named = world.CreateEntity();
    const JsonValue declaredRecord = *JsonParse(text);
    JsonReadArchive declared(declaredRecord);
    ASSERT_TRUE(serializer.LoadIntoWorld(declared, named, world, context));
    EXPECT_EQ(world.TryGet<Projectile>(named)->PassesThrough, glass);

    std::string misspelled = text;
    misspelled.replace(misspelled.find("material.glass"), 14, "material.glas");
    const EntityId misnamed = world.CreateEntity();
    const JsonValue undeclaredRecord = *JsonParse(misspelled);
    JsonReadArchive undeclared(undeclaredRecord);
    EXPECT_FALSE(serializer.LoadIntoWorld(undeclared, misnamed, world, context));
}
