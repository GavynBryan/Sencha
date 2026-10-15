// The vocabulary a project declares as content -- tags, attributes, effects -- is
// registered when content is published, in asset path order, and a declaration
// that does not compile or resolve is reported rather than skipped.

#include <assets/runtime/ContentVocabulary.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityKit.h>
#include <app/EngineVocabulary.h>
#include <authored/WorldVocabulary.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <attributes/AttributeDeclarations.h>
#include <core/json/JsonParser.h>
#include <ecs/World.h>
#include <effects/EffectDefinitionData.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace
{
    struct Content
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() / "sencha_content_tags";
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };

        Content()
        {
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root / "b");
            std::filesystem::create_directories(Root / "a");
        }
        ~Content() { std::filesystem::remove_all(Root); }

        void Write(const std::string& relative, std::string_view text)
        {
            std::ofstream(Root / relative) << text;
        }

        void Scan() { (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds()); }
    };
}

TEST(ContentVocabulary, EveryDeclaredNameIsRegisteredInPathOrder)
{
    Content content;
    content.Write("b/walker.tags.sdata", R"({ "type": "gameplay.tag_declarations", "version": 1,
        "data": { "tags": [ "Anim.Walk", "Anim.Idle" ] } })");
    content.Write("a/door.tags.sdata", R"({ "type": "gameplay.tag_declarations", "version": 1,
        "data": { "tags": [ "Anim.Door.Open" ] } })");
    content.Scan();

    GameplayTagRegistry tags;
    std::vector<std::string> errors;
    DeclareContentTags(content.Assets, tags, errors);
    EXPECT_TRUE(errors.empty()) << errors.front();
    ASSERT_TRUE(tags.FindTag("Anim.Door.Open").IsValid());
    ASSERT_TRUE(tags.FindTag("Anim.Walk").IsValid());
    ASSERT_TRUE(tags.FindTag("Anim.Idle").IsValid());
    // a/ before b/, then list order: the same numbering on every machine.
    EXPECT_LT(tags.FindTag("Anim.Door.Open").Value, tags.FindTag("Anim.Walk").Value);
    EXPECT_LT(tags.FindTag("Anim.Walk").Value, tags.FindTag("Anim.Idle").Value);

    // Declaring again changes nothing.
    const GameplayTagId walk = tags.FindTag("Anim.Walk");
    DeclareContentTags(content.Assets, tags, errors);
    EXPECT_EQ(tags.FindTag("Anim.Walk"), walk);
}

TEST(ContentVocabulary, ATagDeclaredReplicatedToAllIsMarkedSo)
{
    Content content;
    content.Write("a/weapon.tags.sdata", R"({ "type": "gameplay.tag_declarations", "version": 1,
        "data": { "tags": [ "State.Charged" ], "replicate_to_all": [ "Weapon.Firing" ] } })");
    content.Scan();

    GameplayTagRegistry tags;
    std::vector<std::string> errors;
    DeclareContentTags(content.Assets, tags, errors);
    EXPECT_TRUE(errors.empty()) << errors.front();
    ASSERT_TRUE(tags.FindTag("Weapon.Firing").IsValid()) << "declared by being listed";
    EXPECT_TRUE(tags.IsReplicatedToAll(tags.FindTag("Weapon.Firing")));
    EXPECT_FALSE(tags.IsReplicatedToAll(tags.FindTag("State.Charged")));
    EXPECT_FALSE(tags.IsReplicatedToAll(tags.FindTag("Weapon"))) << "a parent is not marked by its child";
}

TEST(ContentVocabulary, AMalformedDeclarationIsReported)
{
    Content content;
    content.Write("a/bad.tags.sdata", R"({ "type": "gameplay.tag_declarations", "version": 1,
        "data": { "tags": [ "not a tag" ] } })");
    content.Scan();
    GameplayTagRegistry tags;
    std::vector<std::string> errors;
    DeclareContentTags(content.Assets, tags, errors);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors.front().find("bad.tags.sdata"), std::string::npos) << errors.front();
}

namespace
{
    // A World holding the kit's registries, as the runtime and every editor document has.
    struct VocabularyWorld
    {
        World Entities;
        VocabularyWorld() { InstallAbilityKitVocabulary(Entities); }
        AttributeRegistry& Attributes() { return Entities.GetResource<AttributeRegistry>(); }
        EffectRegistry& Effects() { return Entities.GetResource<EffectRegistry>(); }
    };

    constexpr std::string_view kTags = R"({ "type": "gameplay.tag_declarations", "version": 1,
        "data": { "tags": [ "Cooldown.Charge" ] } })";
    constexpr std::string_view kAttributes = R"({ "type": "gameplay.attribute_declarations", "version": 1,
        "data": { "attributes": [ { "key": "ammo", "name": "Ammo", "min": 0, "max": 30, "default": 30 },
                                  { "key": "hp", "name": "Health", "min": 0, "max": 100, "default": 100 } ] } })";
}

TEST(ContentVocabulary, AttributesAndEffectsAreDeclaredAndResolved)
{
    Content content;
    content.Write("a/game.tags.sdata", kTags);
    content.Write("a/game.attributes.sdata", kAttributes);
    content.Write("b/charge_cooldown.sdata", R"({ "type": "effect.definition", "version": 1, "data": {
        "duration": "duration", "seconds": 0.5, "granted_tags": [ "Cooldown.Charge" ] } })");
    content.Write("b/ammo_1.sdata", R"({ "type": "effect.definition", "version": 1, "data": {
        "duration": "instant", "modifiers": [ { "key": "m", "attribute": "Ammo", "op": "add", "magnitude": -1 } ] } })");
    content.Scan();

    VocabularyWorld world;
    std::vector<std::string> errors;
    DeclareContentVocabulary(content.Assets, world.Entities, ContentVocabularyReload::Once, errors);
    ASSERT_TRUE(errors.empty()) << errors.front();

    const AttributeId ammo = world.Attributes().FindAttribute("Ammo");
    ASSERT_TRUE(ammo.IsValid());
    EXPECT_FLOAT_EQ(world.Attributes().Max(ammo), 30.0f);

    const EffectDefinition* cost = world.Effects().Get(world.Effects().Find("asset://b/ammo_1.sdata"));
    ASSERT_NE(cost, nullptr) << "an effect is named by its asset path";
    ASSERT_EQ(cost->Modifiers.size(), 1u);
    EXPECT_EQ(cost->Modifiers[0].Attr, ammo);
    const EffectDefinition* cooldown = world.Effects().Get(world.Effects().Find("asset://b/charge_cooldown.sdata"));
    ASSERT_NE(cooldown, nullptr);
    EXPECT_EQ(cooldown->Duration, EffectDuration::Duration);
    ASSERT_EQ(cooldown->GrantedTags.size(), 1u);
}

TEST(ContentVocabulary, AnEffectNamingAnUndeclaredAttributeIsReported)
{
    Content content;
    content.Write("a/stray.sdata", R"({ "type": "effect.definition", "version": 1, "data": {
        "duration": "instant", "modifiers": [ { "key": "m", "attribute": "Mana", "magnitude": -1 } ] } })");
    content.Scan();
    VocabularyWorld world;
    std::vector<std::string> errors;
    DeclareContentVocabulary(content.Assets, world.Entities, ContentVocabularyReload::Once, errors);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors.front().find("Mana"), std::string::npos) << errors.front();
    EXPECT_FALSE(world.Effects().Find("asset://a/stray.sdata").IsValid());
}

TEST(ContentVocabulary, AnAttributeDeclaredTwiceDifferentlyIsReportedAndTheFirstStands)
{
    Content content;
    content.Write("a/one.attributes.sdata", kAttributes);
    content.Write("b/two.attributes.sdata", R"({ "type": "gameplay.attribute_declarations", "version": 1,
        "data": { "attributes": [ { "key": "ammo", "name": "Ammo", "min": 0, "max": 99, "default": 30 } ] } })");
    content.Scan();
    VocabularyWorld world;
    std::vector<std::string> errors;
    DeclareContentVocabulary(content.Assets, world.Entities, ContentVocabularyReload::Once, errors);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_FLOAT_EQ(world.Attributes().Max(world.Attributes().FindAttribute("Ammo")), 30.0f);
}

TEST(ContentVocabulary, AReloadedEffectIsRedefinedUnderTheSameId)
{
    Content content;
    content.Write("a/game.attributes.sdata", kAttributes);
    content.Write("b/hit.sdata", R"({ "type": "effect.definition", "version": 1, "data": {
        "duration": "instant", "modifiers": [ { "key": "m", "attribute": "Health", "magnitude": -10 } ] } })");
    content.Scan();
    VocabularyWorld world;
    std::vector<std::string> errors;
    DeclareContentVocabulary(content.Assets, world.Entities, ContentVocabularyReload::Watch, errors);
    ASSERT_TRUE(errors.empty()) << errors.front();
    const EffectId hit = world.Effects().Find("asset://b/hit.sdata");
    ASSERT_TRUE(hit.IsValid());

    const DataAssetTypeRegistration* type = content.Assets.DataTypes.Find(kEffectDefinitionType);
    ASSERT_NE(type, nullptr);
    const DataAssetCompileResult harder = type->Compile(*JsonParse(R"({ "duration": "instant",
        "modifiers": [ { "key": "m", "attribute": "Health", "magnitude": -40 } ] })"));
    ASSERT_TRUE(harder.IsValid()) << harder.Error;
    ASSERT_TRUE(content.Assets.DataAssets.ReloadInPlace("asset://b/hit.sdata", kEffectDefinitionType, harder.Value));

    RefreshContentVocabulary(content.Assets, world.Entities, errors);
    ASSERT_TRUE(errors.empty()) << errors.front();
    EXPECT_EQ(world.Effects().Find("asset://b/hit.sdata"), hit) << "every reference to the effect still means it";
    EXPECT_FLOAT_EQ(world.Effects().Get(hit)->Modifiers[0].Magnitude, -40.0f);
    ReleaseContentVocabulary(world.Entities);
}

TEST(ContentVocabulary, TheNormativeChargeIsDeclaredFromItsContentRoot)
{
    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets{ logging, serializers };
    const std::string root = std::string(SENCHA_REPO_ROOT) + "/test/fixtures/abilities/assets";
    (void)ScanAssetsDirectory(root, assets.Registry, assets.Assets.Kinds());

    VocabularyWorld world;
    world.Entities.RegisterComponent<AttributeSet>();
    InstallAuthoredVocabulary(world.Entities);
    ASSERT_TRUE(InstallEngineVocabulary(world.Entities));
    std::vector<std::string> errors;
    DeclareContentVocabulary(assets, world.Entities, ContentVocabularyReload::Once, errors);
    ASSERT_TRUE(errors.empty()) << errors.front();

    const AbilityRegistry& abilities = world.Entities.GetResource<AbilityRegistry>();
    const AbilityDefinition* charge = abilities.Get(abilities.Find("asset://abilities/charge_release.sdata"));
    ASSERT_NE(charge, nullptr) << "an ability is named by its asset path";
    ASSERT_EQ(charge->Phases.size(), 3u);
    ASSERT_EQ(charge->Costs.size(), 1u);
    EXPECT_EQ(charge->Costs[0].Effects.front(), world.Effects().Find("asset://effects/ammo_1.sdata"))
        << "effects are declared before the abilities that name them";
}

TEST(ContentVocabulary, AnAbilityNamingAnUnknownVerbIsReportedAndKeepsItsName)
{
    Content content;
    content.Write("a/broken.sdata", R"({ "type": "ability.definition", "version": 1, "data": {
        "phases": [ { "key": "p_a", "enter": [ { "key": "o_x", "op": "invoke", "verb": "no.such" } ],
                      "then": [ { "key": "t_end", "to": "end" } ] } ] } })");
    content.Scan();
    VocabularyWorld world;
    InstallAuthoredVocabulary(world.Entities);
    std::vector<std::string> errors;
    DeclareContentVocabulary(content.Assets, world.Entities, ContentVocabularyReload::Once, errors);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors.front().find("verb_unknown o_x"), std::string::npos) << errors.front();
    const AbilityRegistry& abilities = world.Entities.GetResource<AbilityRegistry>();
    const AbilityId broken = abilities.Find("asset://a/broken.sdata");
    EXPECT_TRUE(broken.IsValid()) << "reserved, so a grant naming it still saves";
    EXPECT_EQ(abilities.Get(broken), nullptr);
}

TEST(ContentVocabulary, AnEffectDeclaresTheAbilitiesItGrants)
{
    Content content;
    content.Write("a/fire.sdata", R"({ "type": "ability.definition", "version": 1, "data": {
        "phases": [ { "key": "p_a", "then": [ { "key": "t_end", "to": "end" } ] } ] } })");
    content.Write("b/rifle.sdata", R"({ "type": "effect.definition", "version": 1, "data": {
        "duration": "infinite",
        "granted_abilities": [ { "key": "w_fire", "ability": { "data": "asset://a/fire.sdata" },
                                 "binding": { "action": "Input.PrimaryFire", "priority": 10 } } ] } })");
    content.Scan();
    VocabularyWorld world;
    std::vector<std::string> errors;
    DeclareContentVocabulary(content.Assets, world.Entities, ContentVocabularyReload::Once, errors);
    ASSERT_TRUE(errors.empty()) << errors.front();

    const EffectId rifle = world.Effects().Find("asset://b/rifle.sdata");
    const std::span<const std::uint16_t> granted = world.Entities.GetResource<AbilityEffectGrants>().Of(rifle);
    ASSERT_EQ(granted.size(), 1u);
    const AbilityGrantEntry* entry = world.Entities.GetResource<AbilityGrantEntries>().Find(granted.front());
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->Key, "w_fire");
    EXPECT_EQ(entry->Override.Priority, 10);
    EXPECT_EQ(world.Entities.GetResource<AbilityRegistry>().GetName(entry->Ability), "asset://a/fire.sdata");
}

TEST(ContentVocabulary, MalformedDeclarationsDoNotCompile)
{
    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets{ logging, serializers };
    const auto compile = [&](std::string_view type, std::string_view data) {
        return assets.DataTypes.Find(type)->Compile(*JsonParse(data));
    };
    EXPECT_FALSE(compile(kEffectDefinitionType, R"({ "duration": "duration", "seconds": 0 })").IsValid());
    EXPECT_FALSE(compile(kEffectDefinitionType, R"({ "granted_abilities": [ { "key": "g",
        "ability": { "data": "asset://a.sdata" } } ] })").IsValid()) << "an instant effect cannot hold a grant";
    EXPECT_FALSE(compile(kEffectDefinitionType, R"({ "duration": "instant", "granted_tags": [ "A" ] })").IsValid());
    EXPECT_FALSE(compile(kEffectDefinitionType,
                         R"({ "duration": "infinite", "granted_tags": [ "A", "B", "C", "D", "E", "F", "G", "H", "I" ] })")
                     .IsValid());
    EXPECT_FALSE(compile(kAttributeDeclarationsType,
                         R"({ "attributes": [ { "key": "a", "name": "A", "min": 5, "max": 1 } ] })").IsValid());
    EXPECT_FALSE(compile(kAttributeDeclarationsType,
                         R"({ "attributes": [ { "key": "a", "name": "A" }, { "key": "b", "name": "A" } ] })").IsValid());
    EXPECT_TRUE(compile(kAttributeDeclarationsType, R"({ "attributes": [ { "key": "a", "name": "A" } ] })").IsValid());
}
