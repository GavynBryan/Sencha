#include <gtest/gtest.h>

#include <assets/data/DataAssetCache.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementProfileBindingCache.h>
#include <movement/MovementProfileData.h>

#include <core/json/JsonParser.h>
#include <ecs/World.h>
#include <movement/MovementRegistration.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

//=============================================================================
// What happens to a character whose authored movement cannot be bound.
//
// The answer used to be: it silently moves on engine defaults. Every
// coefficient -- speed, friction, jump height, gravity -- quietly replaced,
// with nothing said anywhere, because one layer named a gameplay tag that no
// longer existed. A designer reading that would conclude their tuning numbers
// were wrong and start changing numbers nothing was reading.
//
// The failure is still total, which is the right call: half a profile is a
// character that is harder to diagnose than one that plainly refused. What
// changed is that it says so.
//=============================================================================

namespace
{
    std::shared_ptr<CompiledMovementProfile> ProfileWithLayer(
        MovementProfileLayer layer)
    {
        auto profile = std::make_shared<CompiledMovementProfile>();
        profile->Name = "test_profile";
        profile->Layers.push_back(std::move(layer));
        return profile;
    }

    MovementProfileLayer FrictionLayer(float friction)
    {
        MovementProfileLayer layer;
        layer.Name = "base";
        layer.Set.Friction = friction;
        return layer;
    }

    struct Fixture
    {
        DataAssetCache Assets;
        GameplayTagRegistry Tags;
        LocomotionModeRegistry Modes{ Tags };

        MovementProfileHandle Install(std::shared_ptr<CompiledMovementProfile> profile)
        {
            return MovementProfileHandle{
                Assets.Register("test/profile", "movement.profile", std::move(profile))
            };
        }
    };
}

TEST(MovementProfileBinding, AnAuthoredProfileResolvesWithoutComplaint)
{
    Fixture fixture;
    const MovementProfileHandle handle =
        fixture.Install(ProfileWithLayer(FrictionLayer(4.0f)));

    MovementProfileBindingCache cache(fixture.Assets, fixture.Tags, fixture.Modes);
    std::string error;
    const BoundMovementProfile* bound = cache.Get(handle, &error);

    ASSERT_NE(bound, nullptr);
    EXPECT_TRUE(error.empty());
    ASSERT_EQ(bound->Layers.size(), 1u);
    ASSERT_TRUE(bound->Layers[0].Set.Friction.has_value());
    EXPECT_FLOAT_EQ(*bound->Layers[0].Set.Friction, 4.0f);
}

// The regression. A layer naming a tag nothing registered costs the whole
// profile, so what comes back is empty -- indistinguishable, to a resolver that
// does not ask, from a character that was never authored.
TEST(MovementProfileBinding, AProfileThatCannotBindSaysSoAndResolvesToNothing)
{
    MovementProfileLayer layer = FrictionLayer(4.0f);
    layer.When.AllTags.push_back("movement.jump.requested");

    Fixture fixture;
    const MovementProfileHandle handle = fixture.Install(ProfileWithLayer(std::move(layer)));

    MovementProfileBindingCache cache(fixture.Assets, fixture.Tags, fixture.Modes);
    std::string error;
    const BoundMovementProfile* bound = cache.Get(handle, &error);

    // Empty, which is why this is worth reporting: nothing downstream can tell
    // an empty profile from an absent one, and both end at engine defaults.
    ASSERT_NE(bound, nullptr);
    EXPECT_TRUE(bound->Layers.empty());

    // And the name of what went wrong, so the message can point at the line
    // rather than at the character.
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("movement.jump.requested"), std::string::npos);
}

// Resolution runs every tick for every character. A failure that reported
// itself every one of those would bury the frame it first appeared in.
TEST(MovementProfileBinding, TheSameFailureIsReportedOnceNotEveryTick)
{
    MovementProfileLayer layer = FrictionLayer(4.0f);
    layer.When.AllTags.push_back("nothing.registered.this");

    Fixture fixture;
    const MovementProfileHandle handle = fixture.Install(ProfileWithLayer(std::move(layer)));

    MovementProfileBindingCache cache(fixture.Assets, fixture.Tags, fixture.Modes);
    std::string first;
    (void)cache.Get(handle, &first);
    EXPECT_FALSE(first.empty());

    for (int tick = 0; tick < 8; ++tick)
    {
        std::string again;
        (void)cache.Get(handle, &again);
        EXPECT_TRUE(again.empty()) << "reported again on tick " << tick;
    }
}

//=============================================================================
// The template's shipped movement, on the same terms as its shipped controls.
//
// Content committed to the repo is content a new project starts from, and a
// profile that names something the engine has removed does not fail loudly at
// build time -- it fails at run time, silently, as a character that moves
// wrong. This is the check that turns that into a red test.
//=============================================================================

namespace
{
    std::string ReadRepoFile(std::string_view relativePath)
    {
        const std::filesystem::path path =
            std::filesystem::path(SENCHA_REPO_ROOT) / relativePath;
        std::ifstream file(path);
        if (!file)
            return {};
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }
}

TEST(MovementProfileBinding, TheTemplatesShippedMovementProfileBinds)
{
    const std::string document =
        ReadRepoFile("template/assets/data/player_movement.sdata");
    ASSERT_FALSE(document.empty());

    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    RegisterMovementProfileData(types, schemas);

    const std::optional<JsonValue> parsed = JsonParse(document);
    ASSERT_TRUE(parsed.has_value()) << "player_movement.sdata is not valid json";

    // The loader checks the {type, version, data} envelope and hands `data` to
    // the subtype's compiler, so that is what the schema describes.
    const JsonValue* type_ = parsed->Find("type");
    ASSERT_NE(type_, nullptr);
    EXPECT_EQ(type_->AsString(), "movement.profile");
    const JsonValue* data = parsed->Find("data");
    ASSERT_NE(data, nullptr);

    const DataSchema* schema = schemas.Find("movement.profile");
    ASSERT_NE(schema, nullptr);
    std::vector<DataValidationError> errors;
    ASSERT_TRUE(ValidateDataAgainstSchema(*data, *schema, errors))
        << (errors.empty() ? std::string{} : errors.front().Message);

    const DataAssetTypeRegistration* type = types.Find("movement.profile");
    ASSERT_NE(type, nullptr);
    const DataAssetCompileResult compiled = type->Compile(*data);
    ASSERT_NE(compiled.Value, nullptr) << compiled.Error;

    // Bound against the tags and modes a template world actually has, because
    // that is the only set that decides whether it binds at run time.
    World world;
    RegisterMovement(world);
    GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
    LocomotionModeRegistry& modes = world.GetResource<LocomotionModeRegistry>();

    const MovementProfileBindResult bound = BindMovementProfile(
        *std::static_pointer_cast<const CompiledMovementProfile>(compiled.Value),
        tags,
        [&modes](std::string_view name)
        {
            const LocomotionModeEntry* mode = modes.Find(name);
            return mode != nullptr ? mode->Id : LocomotionModeId{};
        });

    EXPECT_TRUE(bound.IsValid())
        << "the template's movement profile no longer binds: " << bound.Error;
}

// A per-frame error is not a diagnostic, it is a denial of service against the
// log it appears in: one character with an unbound profile produced over a
// thousand identical lines in seventeen seconds of a live session, which is
// where this was found.
TEST(MovementProfileBinding, AnInvalidHandleIsReportedOnceAndNotEveryTick)
{
    Fixture fixture;
    MovementProfileBindingCache cache(fixture.Assets, fixture.Tags, fixture.Modes);

    std::string first;
    EXPECT_EQ(cache.Get(MovementProfileHandle{}, &first), nullptr);
    EXPECT_FALSE(first.empty()) << "an unbound profile said nothing at all";

    for (int call = 0; call < 200; ++call)
    {
        std::string again;
        EXPECT_EQ(cache.Get(MovementProfileHandle{}, &again), nullptr);
        EXPECT_TRUE(again.empty())
            << "call " << call << " reported the same failure again";
    }
}
