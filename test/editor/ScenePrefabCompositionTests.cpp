#include "scene_source/Json5Parser.h"
#include "scene_source/SceneComposition.h"
#include "scene_source/SceneSourceCache.h"
#include "scene_source/SceneSourceUpgrade.h"

#include <core/metadata/DataSchema.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

namespace
{
class MemorySources final : public ISceneSourceLookup
{
public:
    void Add(std::string path, std::string_view text)
    {
        std::string error;
        std::optional<SceneSourceDocument> parsed = ParseSceneSource(text, &error);
        ASSERT_TRUE(parsed.has_value()) << path << ": " << error;
        Docs.emplace(std::move(path), std::move(*parsed));
    }

    const SceneSourceDocument* Find(std::string_view assetPath) override
    {
        const auto found = Docs.find(std::string(assetPath));
        return found != Docs.end() ? &found->second : nullptr;
    }

private:
    std::map<std::string, SceneSourceDocument> Docs;
};

class TableRules final : public ISceneComponentRules
{
public:
    TableRules()
    {
        Schemas.emplace("Attributes", MakeKeyedListRecordSchema("attributes", "name"));
        Schemas.emplace("GameplayTags", MakeKeyedListRecordSchema("tags", "name"));
        Schemas.emplace("AbilityGrants", MakeKeyedListRecordSchema("grants", "key"));
        DataFieldSchema entry = MakeDataField(DataFieldKind::Record, {}, {});
        entry.Children.push_back(MakeDataField(DataFieldKind::Entity, "anchor", "Anchor"));
        Schemas.emplace("Tether", std::move(entry));
        for (auto& [key, schema] : Schemas)
        {
            DataFieldSchema component = schema;
            component.Key = key;
            component.Required = false;
            Entity.Children.push_back(std::move(component));
        }
    }

    const DataFieldSchema& EntitySchema() const override { return Entity; }

    std::vector<std::string> OwedBy(std::string_view component) const override
    {
        if (component == "AbilityGrants")
            return { "GameplayTags" };
        return {};
    }

    bool IsRemovable(std::string_view component) const override { return component != "Transform"; }

private:
    std::map<std::string, DataFieldSchema> Schemas;
    DataFieldSchema Entity = MakeDataField(DataFieldKind::Record, {}, {});
};

constexpr std::string_view kHumanoid = R"({
  format_version: 3,
  entities: [
    { id: '00000000000000a1', components: {
        Transform: { local: { position: [0, 0, 0] } },
        Attributes: { attributes: [ { name: 'Health', base: 100 }, { name: 'Stealth', base: 5 } ] },
        GameplayTags: { tags: [ { name: 'Creature.Humanoid', stacks: 1 } ] },
        AbilityGrants: { grants: [] } } },
    { id: '00000000000000a2', parent: '00000000000000a1', components: {
        Transform: { local: { position: [0, 1, 0] } }, Body: { mass: 70 } } },
  ],
})";

constexpr std::string_view kAlertable = R"({
  format_version: 3, abstract: true,
  entities: [ { id: '00000000000000f1', components: {
      Alertable: { radius: 5 }, GameplayTags: { tags: [ { name: 'Creature.Alert', stacks: 1 } ] } } } ],
})";

constexpr std::string_view kTorch = R"({
  format_version: 3, abstract: true,
  entities: [ { id: '00000000000000f2', components: { Light: { intensity: 3 } } } ],
})";

// The tether names the post by the post's id in this document.
constexpr std::string_view kTethered = R"({
  format_version: 3,
  entities: [
    { id: '00000000000000b1', components: { Post: {} } },
    { id: '00000000000000b2', components: { Tether: { anchor: '00000000000000b1', length: 3 } } },
    { id: '00000000000000b3', components: { Tether: { anchor: '00000000000000c0', length: 1 } } },
  ],
})";

struct Composed
{
    MemorySources Sources;
    TableRules Rules;
    std::string Error;

    Composed()
    {
        Sources.Add("asset://prefabs/humanoid.sscene", kHumanoid);
        Sources.Add("asset://prefabs/fragments/alertable.sscene", kAlertable);
        Sources.Add("asset://prefabs/fragments/torch.sscene", kTorch);
        Sources.Add("asset://prefabs/tethered.sscene", kTethered);
    }

    std::optional<SceneCompositionResult> Resolve(std::string_view text)
    {
        std::optional<SceneSourceDocument> document = ParseSceneSource(text, &Error);
        EXPECT_TRUE(document.has_value()) << Error;
        if (!document)
            return std::nullopt;
        return ResolveSceneComposition(*document, Sources, &Error, &Rules, { .Provenance = true });
    }
};

const ResolvedSceneEntity* Entity(const SceneCompositionResult& result, std::uint64_t id)
{
    const auto found = std::ranges::find(result.Entities, id, [](const ResolvedSceneEntity& entity) { return entity.Id.Value; });
    return found != result.Entities.end() ? &*found : nullptr;
}

const CompositionDiagnostic* Diagnostic(const SceneCompositionResult& result, std::string_view code)
{
    const auto found = std::ranges::find(result.Diagnostics, code, &CompositionDiagnostic::Code);
    return found != result.Diagnostics.end() ? &*found : nullptr;
}

const Json5Value* Attribute(const ResolvedSceneEntity& entity, std::string_view name)
{
    const Json5Value* attributes = entity.Components.Find("Attributes");
    if (attributes == nullptr)
        return nullptr;
    for (const Json5Value& element : attributes->Find("attributes")->Elements)
        if (element.Find("name")->Text == name)
            return &element;
    return nullptr;
}
} // namespace

TEST(ScenePrefabComposition, AChildAdoptsItsBasesEntitiesAndPatchesThemByKey)
{
    Composed composed;
    const auto result = composed.Resolve(R"({
  format_version: 3,
  extends: 'asset://prefabs/humanoid.sscene',
  entities: [ { id: '00000000000000c1', parent: '00000000000000a1', components: { Torch: {} } } ],
  patch: { '00000000000000a1': { Attributes: { attributes: { Health: { base: 200 }, Stealth: null } } } },
})");
    ASSERT_TRUE(result.has_value()) << composed.Error;
    EXPECT_TRUE(result->Diagnostics.empty()) << result->Diagnostics.front().Code;

    const ResolvedSceneEntity* root = Entity(*result, 0xa1);
    const ResolvedSceneEntity* body = Entity(*result, 0xa2);
    const ResolvedSceneEntity* torch = Entity(*result, 0xc1);
    ASSERT_TRUE(root && body && torch) << "the base's ids are the child's, with no wrapper";
    EXPECT_TRUE(root->IsInherited);
    EXPECT_FALSE(root->Parent.IsValid());
    EXPECT_EQ(body->Parent.Value, 0xa1ull);
    EXPECT_FALSE(torch->IsInherited);
    EXPECT_EQ(torch->Parent.Value, 0xa1ull) << "a local hangs under an inherited entity";

    ASSERT_NE(Attribute(*root, "Health"), nullptr);
    EXPECT_EQ(Attribute(*root, "Health")->Find("base")->Number, 200.0);
    EXPECT_EQ(Attribute(*root, "Stealth"), nullptr) << "null removes a keyed element";
    EXPECT_NE(root->SourceComponents.Find("Attributes")->Find("attributes")->Elements.size(), 1u)
        << "the baseline is what the child inherits, before its own patch";

    const CompositionOrigin* set = FindCompositionOrigin(root->Provenance, "$.Attributes.attributes{Health}.base");
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->Layer, CompositionLayer::Local);
    const CompositionOrigin* inherited = FindCompositionOrigin(root->Provenance, "$.Transform.local.position");
    ASSERT_NE(inherited, nullptr);
    EXPECT_EQ(inherited->Layer, CompositionLayer::Base);
    EXPECT_EQ(inherited->Asset, "asset://prefabs/humanoid.sscene");
}

TEST(ScenePrefabComposition, FragmentsMergeOntoTheRootOrTheEntityTheyName)
{
    Composed composed;
    const auto result = composed.Resolve(R"({
  format_version: 3,
  extends: 'asset://prefabs/humanoid.sscene',
  includes: [ { fragment: 'asset://prefabs/fragments/alertable.sscene' },
              { fragment: 'asset://prefabs/fragments/torch.sscene', onto: '00000000000000a2' } ],
})");
    ASSERT_TRUE(result.has_value()) << composed.Error;
    EXPECT_TRUE(result->Diagnostics.empty()) << result->Diagnostics.front().Code;
    const ResolvedSceneEntity* root = Entity(*result, 0xa1);
    ASSERT_NE(root->Components.Find("Alertable"), nullptr);
    EXPECT_EQ(root->Components.Find("GameplayTags")->Find("tags")->Elements.size(), 2u)
        << "the fragment's tag joins the base's by name";
    EXPECT_NE(Entity(*result, 0xa2)->Components.Find("Light"), nullptr);
    const CompositionOrigin* origin = FindCompositionOrigin(root->Provenance, "$.Alertable.radius");
    ASSERT_NE(origin, nullptr);
    EXPECT_EQ(origin->Layer, CompositionLayer::Include);
    EXPECT_EQ(origin->Asset, "asset://prefabs/fragments/alertable.sscene");
}

TEST(ScenePrefabComposition, FragmentsThatDisagreeConflictUntilTheChildStatesTheValue)
{
    Composed composed;
    composed.Sources.Add("asset://prefabs/fragments/wary.sscene", R"({
  format_version: 3, abstract: true,
  entities: [ { id: '00000000000000f3', components: { Alertable: { radius: 9 } } } ],
})");
    constexpr std::string_view kBoth = R"({
  format_version: 3,
  extends: 'asset://prefabs/humanoid.sscene',
  includes: [ { fragment: 'asset://prefabs/fragments/alertable.sscene' },
              { fragment: 'asset://prefabs/fragments/wary.sscene' } ],
  %s
})";
    const auto torn = composed.Resolve(std::string(kBoth).replace(std::string(kBoth).find("%s"), 2, ""));
    ASSERT_TRUE(torn.has_value()) << composed.Error;
    const CompositionDiagnostic* conflict = Diagnostic(*torn, "include_conflict");
    ASSERT_NE(conflict, nullptr);
    EXPECT_EQ(conflict->Path, "00000000000000a1.Alertable.radius");

    const auto settled = composed.Resolve(std::string(kBoth).replace(
        std::string(kBoth).find("%s"), 2, "patch: { '00000000000000a1': { Alertable: { radius: 7 } } },"));
    ASSERT_TRUE(settled.has_value()) << composed.Error;
    EXPECT_EQ(Diagnostic(*settled, "include_conflict"), nullptr) << "stating the value here settles it";
    EXPECT_EQ(Entity(*settled, 0xa1)->Components.Find("Alertable")->Find("radius")->Number, 7.0);
}

TEST(ScenePrefabComposition, RemovingAComponentAnotherOwesOrThatCannotGoIsAnError)
{
    Composed composed;
    const auto result = composed.Resolve(R"({
  format_version: 3,
  extends: 'asset://prefabs/humanoid.sscene',
  patch: { '00000000000000a1': { GameplayTags: null, Transform: null }, '00000000000000a2': { Body: null } },
})");
    ASSERT_TRUE(result.has_value()) << composed.Error;
    const CompositionDiagnostic* owed = Diagnostic(*result, "removes_owed");
    ASSERT_NE(owed, nullptr);
    EXPECT_NE(owed->Message.find("AbilityGrants"), std::string::npos) << owed->Message;
    EXPECT_NE(Diagnostic(*result, "not_removable"), nullptr);
    const ResolvedSceneEntity* root = Entity(*result, 0xa1);
    EXPECT_NE(root->Components.Find("GameplayTags"), nullptr) << "a refused removal removes nothing";
    const ResolvedSceneEntity* body = Entity(*result, 0xa2);
    EXPECT_EQ(body->Components.Find("Body"), nullptr);
    EXPECT_EQ(body->RemovedComponents, std::vector<std::string>{ "Body" });
}

TEST(ScenePrefabComposition, WhatCannotComposeIsReported)
{
    Composed composed;
    composed.Sources.Add("asset://prefabs/fragments/pair.sscene", R"({
  format_version: 3, abstract: true,
  entities: [ { id: '00000000000000f4', components: {} }, { id: '00000000000000f5', components: {} } ],
})");
    const auto shaped = composed.Resolve(R"({
  format_version: 3, extends: 'asset://prefabs/humanoid.sscene',
  includes: [ { fragment: 'asset://prefabs/fragments/pair.sscene' } ],
  patch: { '00000000000000ee': { Body: { mass: 1 } } },
})");
    ASSERT_TRUE(shaped.has_value()) << composed.Error;
    EXPECT_NE(Diagnostic(*shaped, "fragment_shape"), nullptr);
    EXPECT_FALSE(shaped->DanglingOverrides.empty()) << "a patch for an id nothing inherits";

    EXPECT_FALSE(composed.Resolve(R"({
  format_version: 3, extends: 'asset://prefabs/humanoid.sscene',
  entities: [ { id: '00000000000000a2', components: {} } ],
})").has_value());
    EXPECT_NE(composed.Error.find("inherited"), std::string::npos) << composed.Error;

    const auto orphaned = composed.Resolve(R"({
  format_version: 3, extends: 'asset://prefabs/humanoid.sscene',
  entities: [ { id: '00000000000000c1', parent: '00000000000000ee', components: {} } ],
})");
    ASSERT_TRUE(orphaned.has_value()) << composed.Error;
    EXPECT_NE(Diagnostic(*orphaned, "dangling_parent"), nullptr);

    composed.Sources.Add("asset://prefabs/loop_a.sscene",
                         "{ format_version: 3, extends: 'asset://prefabs/loop_b.sscene', entities: [] }");
    composed.Sources.Add("asset://prefabs/loop_b.sscene",
                         "{ format_version: 3, extends: 'asset://prefabs/loop_a.sscene', entities: [] }");
    EXPECT_FALSE(composed.Resolve("{ format_version: 3, extends: 'asset://prefabs/loop_a.sscene', entities: [] }")
                     .has_value());
    EXPECT_NE(composed.Error.find("cycle"), std::string::npos) << composed.Error;
}

TEST(ScenePrefabComposition, AnInstancePatchMergesByKey)
{
    Composed composed;
    const auto result = composed.Resolve(R"({
  format_version: 3, entities: [],
  instances: [ { id: '0000000000000100', source: 'asset://prefabs/humanoid.sscene',
    entity_ids: { '00000000000000a1': '0000000000000101', '00000000000000a2': '0000000000000102' },
    patch: { '00000000000000a1': { Attributes: { attributes: { Health: { base: 150 } } } } } } ],
})");
    ASSERT_TRUE(result.has_value()) << composed.Error;
    const ResolvedSceneEntity* member = Entity(*result, 0x101);
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(Attribute(*member, "Health")->Find("base")->Number, 150.0);
    EXPECT_NE(Attribute(*member, "Stealth"), nullptr) << "a keyed patch leaves the other elements";
}

TEST(ScenePrefabComposition, EachPlacementsMembersNameEachOtherByThatPlacementsIds)
{
    Composed composed;
    const auto result = composed.Resolve(R"({
  format_version: 3, entities: [],
  instances: [
    { id: '0000000000000200', source: 'asset://prefabs/tethered.sscene',
      entity_ids: { '00000000000000b1': '0000000000000201', '00000000000000b2': '0000000000000202',
                    '00000000000000b3': '0000000000000203' } },
    { id: '0000000000000300', source: 'asset://prefabs/tethered.sscene',
      entity_ids: { '00000000000000b1': '0000000000000301', '00000000000000b2': '0000000000000302',
                    '00000000000000b3': '0000000000000303' } } ],
})");
    ASSERT_TRUE(result.has_value()) << composed.Error;
    const auto anchor = [&](std::uint64_t id) {
        const ResolvedSceneEntity* entity = Entity(*result, id);
        return entity != nullptr ? entity->Components.Find("Tether")->Find("anchor")->Text : std::string();
    };
    EXPECT_EQ(anchor(0x202), "0000000000000201");
    EXPECT_EQ(anchor(0x302), "0000000000000301");
    EXPECT_EQ(anchor(0x203), "00000000000000c0") << "an id that names no member is not the placement's to change";
}

TEST(ScenePrefabComposition, AReferenceToAMemberThePlacementDropsIsReported)
{
    Composed composed;
    const auto result = composed.Resolve(R"({
  format_version: 3, entities: [],
  instances: [ { id: '0000000000000200', source: 'asset://prefabs/tethered.sscene',
    entity_ids: { '00000000000000b1': '0000000000000201', '00000000000000b2': '0000000000000202',
                  '00000000000000b3': '0000000000000203' },
    suppress: [ '00000000000000b1' ] } ],
})");
    ASSERT_TRUE(result.has_value()) << composed.Error;
    const CompositionDiagnostic* dangling = Diagnostic(*result, "dangling_member_reference");
    ASSERT_NE(dangling, nullptr);
    EXPECT_EQ(dangling->Path, "0000000000000202");
}

// The list's order must not decide whether a removal is allowed.
TEST(ScenePrefabComposition, RemovingAComponentWithEverythingThatOwesItIsAllowedInAnyOrder)
{
    Composed composed;
    const auto result = composed.Resolve(R"({
  format_version: 3,
  extends: 'asset://prefabs/humanoid.sscene',
  patch: { '00000000000000a1': { GameplayTags: null, AbilityGrants: null } },
})");
    ASSERT_TRUE(result.has_value()) << composed.Error;
    EXPECT_EQ(Diagnostic(*result, "removes_owed"), nullptr);
    const ResolvedSceneEntity* humanoid = Entity(*result, 0xa1);
    EXPECT_EQ(humanoid->Components.Find("GameplayTags"), nullptr);
    EXPECT_EQ(humanoid->Components.Find("AbilityGrants"), nullptr);
}

TEST(ScenePrefabComposition, ALocalWhoseInheritedParentIsGoneIsReportedNotFatal)
{
    Composed composed;
    const auto result = composed.Resolve(R"({
  format_version: 3,
  extends: 'asset://prefabs/humanoid.sscene',
  entities: [ { id: '00000000000000c1', parent: '00000000000000ff', components: {} } ],
})");
    ASSERT_TRUE(result.has_value()) << composed.Error;
    ASSERT_NE(Diagnostic(*result, "dangling_parent"), nullptr);
    const ResolvedSceneEntity* local = Entity(*result, 0xc1);
    ASSERT_NE(local, nullptr);
    EXPECT_FALSE(local->Parent.IsValid());
}

TEST(ScenePrefabComposition, AFragmentThatExtendsAnotherIsNotIncluded)
{
    Composed composed;
    composed.Sources.Add("asset://prefabs/fragments/bright_torch.sscene", R"({
  format_version: 3, abstract: true, extends: 'asset://prefabs/fragments/torch.sscene',
  patch: { '00000000000000f2': { Light: { intensity: 9 } } } })");
    const auto result = composed.Resolve(R"({
  format_version: 3, extends: 'asset://prefabs/humanoid.sscene',
  includes: [ { fragment: 'asset://prefabs/fragments/bright_torch.sscene' } ],
})");
    ASSERT_TRUE(result.has_value()) << composed.Error;
    EXPECT_NE(Diagnostic(*result, "include_extends"), nullptr);
    EXPECT_EQ(Entity(*result, 0xa1)->Components.Find("Light"), nullptr);
}

namespace
{
const std::filesystem::path kFixtureRoot = std::filesystem::path(SENCHA_REPO_ROOT) / "test/fixtures/scene_source";

std::string ReadFixture(std::string_view relative)
{
    std::ifstream in(kFixtureRoot / relative);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

const Json5Value* Binding(const ResolvedSceneEntity& entity, std::string_view key)
{
    return entity.Components.Find("responses")->Find("bindings")->Find(key);
}
} // namespace

TEST(ScenePrefabComposition, AnUpgradedVersionOnePlacementComposesAsVersionOneDid)
{
    SceneSourceCache sources({ kFixtureRoot });
    TableRules rules;
    std::string error;
    const std::optional<SceneSourceUpgrade> upgrade = UpgradeSceneSourceText(R"({
  format_version: 3,
  entities: [],
  instances: [
    {
      id: "0000000000000100",
      source: "asset://prefabs/humanoid.sscene",
      transform: { position: [4, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
      entity_ids: { "00000000000000a1": "0000000000000101" },
      patch: {
        "00000000000000a1": {
          Attributes: { attributes: [{ name: "Health", base: 150 }] },
          responses: { bindings: { struck: { cooldown: 2 } } },
        },
      },
    },
  ],
})", &error);
    ASSERT_TRUE(upgrade.has_value()) << error;
    ASSERT_TRUE(upgrade->Refused.empty());
    std::optional<SceneSourceDocument> level = ParseSceneSource(upgrade->Text, &error);
    ASSERT_TRUE(level.has_value()) << error;

    const auto result = ResolveSceneComposition(*level, sources, &error, &rules);
    ASSERT_TRUE(result.has_value()) << error;
    const ResolvedSceneEntity* placed = Entity(*result, 0x101);
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(Attribute(*placed, "Health")->Find("base")->Number, 150.0);
    EXPECT_EQ(Attribute(*placed, "Stealth"), nullptr) << "a list patch on an override list replaces the list";
    EXPECT_EQ(Binding(*placed, "struck")->Find("cooldown")->Number, 2.0);
    EXPECT_NE(Binding(*placed, "used"), nullptr);
}

TEST(ScenePrefabComposition, AFixtureComposesThroughEveryLayer)
{
    SceneSourceCache sources({ kFixtureRoot });
    TableRules rules;
    std::string error;
    const std::string text = ReadFixture("levels/placed.sscene");
    std::optional<SceneSourceDocument> level = ParseSceneSource(text, &error);
    ASSERT_TRUE(level.has_value()) << error;
    const std::string written = WriteSceneSource(*level);
    EXPECT_EQ(WriteSceneSource(*ParseSceneSource(written, &error)), written);

    const auto result = ResolveSceneComposition(*level, sources, &error, &rules);
    ASSERT_TRUE(result.has_value()) << error;
    EXPECT_TRUE(result->Diagnostics.empty());
    EXPECT_TRUE(result->DanglingOverrides.empty());
    EXPECT_EQ(result->Sources.size(), 3u) << "the guard, its base and its fragment";
    const ResolvedSceneEntity* guard = Entity(*result, 0x201);
    ASSERT_NE(guard, nullptr);
    EXPECT_EQ(Attribute(*guard, "Health")->Find("base")->Number, 200.0) << "the guard's keyed patch";
    EXPECT_EQ(Attribute(*guard, "Stealth"), nullptr) << "the guard's null";
    EXPECT_EQ(guard->Components.Find("Alertable")->Find("radius")->Number, 5.0) << "the fragment";
    EXPECT_EQ(guard->Components.Find("GameplayTags")->Find("tags")->Elements.size(), 3u)
        << "the base's tag, the fragment's, and the placement's";
    EXPECT_EQ(Binding(*guard, "used"), nullptr) << "removed by the guard";
    EXPECT_EQ(Binding(*guard, "struck"), nullptr) << "removed by the placement";
}

namespace
{
std::optional<SceneCompositionResult> ExtendsChain(Composed& composed, std::size_t nested)
{
    for (std::size_t i = 0; i + 1 < nested; ++i)
        composed.Sources.Add(std::format("asset://prefabs/link{}.sscene", i),
                             std::format("{{ format_version: 3, extends: 'asset://prefabs/link{}.sscene', entities: [] }}", i + 1));
    composed.Sources.Add(std::format("asset://prefabs/link{}.sscene", nested - 1), kHumanoid);
    return composed.Resolve("{ format_version: 3, extends: 'asset://prefabs/link0.sscene', entities: [] }");
}
} // namespace

TEST(ScenePrefabComposition, AChainOfSixteenScenesResolvesAndSeventeenDoesNot)
{
    Composed sixteen;
    EXPECT_TRUE(ExtendsChain(sixteen, 15).has_value()) << sixteen.Error;
    Composed seventeen;
    EXPECT_FALSE(ExtendsChain(seventeen, 16).has_value());
    EXPECT_NE(seventeen.Error.find("deeper than 16"), std::string::npos) << seventeen.Error;
}

TEST(ScenePrefabComposition, PlacementsNestNoDeeperThanSixteenScenes)
{
    Composed composed;
    for (int i = 0; i < 17; ++i)
        composed.Sources.Add(std::format("asset://prefabs/nest{}.sscene", i),
                             std::format("{{ format_version: 3, entities: [], instances: [ {{ id: '{:016x}', "
                                         "source: 'asset://prefabs/nest{}.sscene' }} ] }}",
                                         0x500 + i, i + 1));
    composed.Sources.Add("asset://prefabs/nest17.sscene", "{ format_version: 3, entities: [] }");
    EXPECT_FALSE(composed.Resolve("{ format_version: 3, entities: [], instances: [ { id: '00000000000004ff', "
                                  "source: 'asset://prefabs/nest0.sscene' } ] }")
                     .has_value());
    EXPECT_NE(composed.Error.find("deeper than 16"), std::string::npos) << composed.Error;
}

TEST(ScenePrefabComposition, ACycleNamesEverySceneOnIt)
{
    Composed composed;
    composed.Sources.Add("asset://prefabs/loop_a.sscene",
                         "{ format_version: 3, extends: 'asset://prefabs/loop_b.sscene', entities: [] }");
    composed.Sources.Add("asset://prefabs/loop_b.sscene",
                         "{ format_version: 3, extends: 'asset://prefabs/loop_a.sscene', entities: [] }");
    EXPECT_FALSE(composed.Resolve("{ format_version: 3, extends: 'asset://prefabs/loop_a.sscene', entities: [] }")
                     .has_value());
    EXPECT_NE(composed.Error.find("asset://prefabs/loop_a.sscene -> asset://prefabs/loop_b.sscene -> "
                                  "asset://prefabs/loop_a.sscene"),
              std::string::npos)
        << composed.Error;
}

TEST(ScenePrefabComposition, ASourceThatDoesNotResolveIsNamedWithTheDocumentAndFieldThatAskedForIt)
{
    Composed composed;
    composed.Sources.Add("asset://prefabs/broken.sscene", R"({
  format_version: 3, extends: 'asset://prefabs/humanoid.sscene',
  includes: [ { fragment: 'asset://prefabs/fragments/gone.sscene' } ],
})");
    EXPECT_FALSE(composed.Resolve("{ format_version: 3, extends: 'asset://prefabs/broken.sscene', entities: [] }")
                     .has_value());
    EXPECT_NE(composed.Error.find("'asset://prefabs/broken.sscene': $.includes[0] names "
                                  "'asset://prefabs/fragments/gone.sscene'"),
              std::string::npos)
        << composed.Error;
}

TEST(ScenePrefabComposition, APatchThatNullsAComponentIsHeldToTheRemovalRules)
{
    Composed composed;
    const auto refused = composed.Resolve(R"({
  format_version: 3, extends: 'asset://prefabs/humanoid.sscene',
  patch: { '00000000000000a1': { Transform: null, GameplayTags: null } },
})");
    ASSERT_TRUE(refused.has_value()) << composed.Error;
    EXPECT_NE(Diagnostic(*refused, "not_removable"), nullptr);
    EXPECT_NE(Diagnostic(*refused, "removes_owed"), nullptr);
    const ResolvedSceneEntity* root = Entity(*refused, 0xa1);
    EXPECT_NE(root->Components.Find("Transform"), nullptr);
    EXPECT_NE(root->Components.Find("GameplayTags"), nullptr) << "a refused removal removes nothing";

    const auto allowed = composed.Resolve(R"({
  format_version: 3, extends: 'asset://prefabs/humanoid.sscene',
  patch: { '00000000000000a1': { GameplayTags: null, AbilityGrants: null } },
})");
    ASSERT_TRUE(allowed.has_value()) << composed.Error;
    EXPECT_EQ(Diagnostic(*allowed, "removes_owed"), nullptr) << "an entity's nulls are one removal list";
    const ResolvedSceneEntity* humanoid = Entity(*allowed, 0xa1);
    EXPECT_EQ(humanoid->Components.Find("GameplayTags"), nullptr);
    EXPECT_EQ(humanoid->Components.Find("AbilityGrants"), nullptr);
    EXPECT_EQ(humanoid->RemovedComponents.size(), 2u);
}

TEST(ScenePrefabComposition, APlacementsRemovalsAreHeldToTheRemovalRules)
{
    Composed composed;
    const auto result = composed.Resolve(R"({
  format_version: 3, entities: [],
  instances: [ { id: '0000000000000100', source: 'asset://prefabs/humanoid.sscene',
    entity_ids: { '00000000000000a1': '0000000000000101', '00000000000000a2': '0000000000000102' },
    patch: { '00000000000000a1': { GameplayTags: null, Transform: null }, '00000000000000a2': { Body: null } } } ],
})");
    ASSERT_TRUE(result.has_value()) << composed.Error;
    EXPECT_NE(Diagnostic(*result, "not_removable"), nullptr);
    EXPECT_NE(Diagnostic(*result, "removes_owed"), nullptr);
    const ResolvedSceneEntity* member = Entity(*result, 0x101);
    ASSERT_NE(member, nullptr);
    EXPECT_NE(member->Components.Find("Transform"), nullptr);
    EXPECT_NE(member->Components.Find("GameplayTags"), nullptr);
    EXPECT_EQ(Entity(*result, 0x102)->Components.Find("Body"), nullptr);
}

TEST(ScenePrefabComposition, ASceneResolvedOnceIsStillHeldToTheDepthWhereverItIsPlacedAgain)
{
    const auto resolve = [](std::size_t placers) {
        Composed composed;
        for (int i = 0; i < 9; ++i)
            composed.Sources.Add(std::format("asset://prefabs/shared{}.sscene", i),
                                 std::format("{{ format_version: 3, extends: 'asset://prefabs/shared{}.sscene', entities: [] }}", i + 1));
        composed.Sources.Add("asset://prefabs/shared9.sscene", "{ format_version: 3, entities: [] }");
        for (std::size_t i = 0; i < placers; ++i)
            composed.Sources.Add(std::format("asset://prefabs/placer{}.sscene", i),
                                 std::format("{{ format_version: 3, entities: [], instances: [ {{ id: '{:016x}', source: '{}' }} ] }}",
                                             0x700 + i,
                                             i + 1 < placers ? std::format("asset://prefabs/placer{}.sscene", i + 1)
                                                             : std::string("asset://prefabs/shared0.sscene")));
        const auto result = composed.Resolve(R"({ format_version: 3, entities: [], instances: [
            { id: '00000000000006f0', source: 'asset://prefabs/shared0.sscene' },
            { id: '00000000000006f1', source: 'asset://prefabs/placer0.sscene' } ] })");
        return std::pair{ result.has_value(), composed.Error };
    };
    const auto [fits, fitsError] = resolve(5);
    EXPECT_TRUE(fits) << fitsError;
    const auto [deep, deepError] = resolve(6);
    EXPECT_FALSE(deep);
    EXPECT_NE(deepError.find("deeper than 16"), std::string::npos) << deepError;
}
