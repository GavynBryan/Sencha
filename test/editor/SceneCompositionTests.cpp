// Composition resolution: nested expansion, recorded-identity remapping,
// inner-to-outer override precedence, structural overrides, and the reported
// (never silent) failure modes -- missing minted ids, dangling targets,
// unresolvable sources, and cycles.

#include "scene_source/SceneComposition.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <string>

namespace
{
    class MemorySources : public ISceneSourceLookup
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

    [[nodiscard]] std::string Hex(std::uint64_t value)
    {
        char buffer[17] = {};
        std::snprintf(buffer, sizeof(buffer), "%016llx",
                      static_cast<unsigned long long>(value));
        return buffer;
    }

    [[nodiscard]] const ResolvedSceneEntity* FindEntity(
        const SceneCompositionResult& result, std::uint64_t id)
    {
        for (const ResolvedSceneEntity& entity : result.Entities)
            if (entity.Id.Value == id)
                return &entity;
        return nullptr;
    }

    // A door prop: a body with a light hanging from it.
    constexpr std::string_view kDoorSource = R"({
  format_version: 1,
  entities: [
    { id: '0000000000000001',
      components: { Transform: { local: { position: [0, 1, 0] } },
                    Body: { hp: 10 } } },
    { id: '0000000000000002', parent: '0000000000000001',
      components: { Transform: { local: { position: [0, 2, 0] } },
                    PointLight: { intensity: 5, range: 3 } } },
  ],
})";

    TEST(SceneComposition, ExpandsAnInstanceUnderItsSyntheticRoot)
    {
        MemorySources sources;
        sources.Add("asset://props/door.sscene", kDoorSource);

        std::string error;
        const auto root = ParseSceneSource(R"({
  format_version: 1,
  entities: [ { id: '00000000000000aa', components: {} } ],
  instances: [
    { id: '0000000000000100', parent: '00000000000000aa',
      source: 'asset://props/door.sscene',
      transform: { position: [5, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
      entity_ids: { '0000000000000001': '0000000000000201',
                    '0000000000000002': '0000000000000202' } },
  ],
})", &error);
        ASSERT_TRUE(root.has_value()) << error;

        const auto result = ResolveSceneComposition(*root, sources, &error);
        ASSERT_TRUE(result.has_value()) << error;
        EXPECT_TRUE(result->MissingIds.empty());
        EXPECT_TRUE(result->DanglingOverrides.empty());
        ASSERT_EQ(result->Entities.size(), 4u); // local + root + two expanded

        const ResolvedSceneEntity* instanceRoot = FindEntity(*result, 0x100);
        ASSERT_NE(instanceRoot, nullptr);
        EXPECT_TRUE(instanceRoot->IsInstanceRoot);
        EXPECT_EQ(instanceRoot->Parent.Value, 0xaaull);
        // The placement is the root's ordinary transform component.
        const Json5Value* transform = instanceRoot->Components.Find("Transform");
        ASSERT_NE(transform, nullptr);
        EXPECT_EQ(transform->Find("local")->Find("position")->Elements[0].Number, 5.0);

        const ResolvedSceneEntity* body = FindEntity(*result, 0x201);
        ASSERT_NE(body, nullptr);
        EXPECT_EQ(body->Parent.Value, 0x100ull); // source root hangs from the instance root
        EXPECT_EQ(body->Instance.Value, 0x100ull);
        EXPECT_EQ(body->Path.ToString(), Hex(0x100) + "/" + Hex(0x1));

        const ResolvedSceneEntity* light = FindEntity(*result, 0x202);
        ASSERT_NE(light, nullptr);
        EXPECT_EQ(light->Parent.Value, 0x201ull); // interior parentage remapped
    }

    TEST(SceneComposition, TwoPlacementsOfOneSourceStayDistinct)
    {
        MemorySources sources;
        sources.Add("asset://props/door.sscene", kDoorSource);

        std::string error;
        const auto root = ParseSceneSource(R"({
  format_version: 1,
  entities: [],
  instances: [
    { id: '0000000000000100', source: 'asset://props/door.sscene',
      entity_ids: { '0000000000000001': '0000000000000201',
                    '0000000000000002': '0000000000000202' } },
    { id: '0000000000000101', source: 'asset://props/door.sscene',
      entity_ids: { '0000000000000001': '0000000000000301',
                    '0000000000000002': '0000000000000302' } },
  ],
})", &error);
        ASSERT_TRUE(root.has_value()) << error;

        const auto result = ResolveSceneComposition(*root, sources, &error);
        ASSERT_TRUE(result.has_value()) << error;
        ASSERT_EQ(result->Entities.size(), 6u);
        EXPECT_NE(FindEntity(*result, 0x201), nullptr);
        EXPECT_NE(FindEntity(*result, 0x301), nullptr);
        EXPECT_EQ(FindEntity(*result, 0x301)->Parent.Value, 0x101ull);
    }

    TEST(SceneComposition, NestedInstancesRemapThroughTheOuterRecord)
    {
        MemorySources sources;
        sources.Add("asset://props/door.sscene", kDoorSource);
        // A room that places the door inside itself.
        sources.Add("asset://rooms/hall.sscene", R"({
  format_version: 1,
  entities: [ { id: '0000000000000010', components: {} } ],
  instances: [
    { id: '0000000000000020', parent: '0000000000000010',
      source: 'asset://props/door.sscene',
      entity_ids: { '0000000000000001': '0000000000000021',
                    '0000000000000002': '0000000000000022' } },
  ],
})");

        std::string error;
        const auto root = ParseSceneSource(R"({
  format_version: 1,
  entities: [],
  instances: [
    { id: '0000000000000100', source: 'asset://rooms/hall.sscene',
      entity_ids: {
        '0000000000000010': '0000000000000401',
        '0000000000000020': '0000000000000402',
        '0000000000000020/0000000000000001': '0000000000000403',
        '0000000000000020/0000000000000002': '0000000000000404',
      } },
  ],
})", &error);
        ASSERT_TRUE(root.has_value()) << error;

        const auto result = ResolveSceneComposition(*root, sources, &error);
        ASSERT_TRUE(result.has_value()) << error;
        EXPECT_TRUE(result->MissingIds.empty()) << result->MissingIds.size();

        // The inner door's synthetic root was remapped by the outer record.
        const ResolvedSceneEntity* innerRoot = FindEntity(*result, 0x402);
        ASSERT_NE(innerRoot, nullptr);
        EXPECT_EQ(innerRoot->Parent.Value, 0x401ull);
        // Its body: innermost instance attribution and a two-hop path.
        const ResolvedSceneEntity* body = FindEntity(*result, 0x403);
        ASSERT_NE(body, nullptr);
        EXPECT_EQ(body->Parent.Value, 0x402ull);
        EXPECT_EQ(body->Path.ToString(),
                  Hex(0x100) + "/" + Hex(0x20) + "/" + Hex(0x1));
    }

    TEST(SceneComposition, OverridesApplyInnerToOuter)
    {
        MemorySources sources;
        sources.Add("asset://props/door.sscene", kDoorSource);
        // The room turns the door light up and recolors nothing else.
        sources.Add("asset://rooms/hall.sscene", R"({
  format_version: 1,
  entities: [],
  instances: [
    { id: '0000000000000020', source: 'asset://props/door.sscene',
      entity_ids: { '0000000000000001': '0000000000000021',
                    '0000000000000002': '0000000000000022' },
      patch: { '0000000000000002': { PointLight: { intensity: 40 } } } },
  ],
})");

        std::string error;
        // The world patches the same light's range; intensity keeps the room's
        // inner override because outer only speaks where it differs.
        const auto root = ParseSceneSource(R"({
  format_version: 1,
  entities: [],
  instances: [
    { id: '0000000000000100', source: 'asset://rooms/hall.sscene',
      entity_ids: {
        '0000000000000020': '0000000000000402',
        '0000000000000020/0000000000000001': '0000000000000403',
        '0000000000000020/0000000000000002': '0000000000000404',
      },
      patch: { '0000000000000020/0000000000000002': { PointLight: { range: 9 } } } },
  ],
})", &error);
        ASSERT_TRUE(root.has_value()) << error;

        const auto result = ResolveSceneComposition(*root, sources, &error);
        ASSERT_TRUE(result.has_value()) << error;
        const ResolvedSceneEntity* light = FindEntity(*result, 0x404);
        ASSERT_NE(light, nullptr);
        const Json5Value* pointLight = light->Components.Find("PointLight");
        ASSERT_NE(pointLight, nullptr);
        EXPECT_EQ(pointLight->Find("intensity")->Number, 40.0); // inner override held
        EXPECT_EQ(pointLight->Find("range")->Number, 9.0);      // outer override applied
    }

    TEST(SceneComposition, StructuralOverridesAddRemoveAndSuppress)
    {
        MemorySources sources;
        sources.Add("asset://props/door.sscene", kDoorSource);

        std::string error;
        const auto root = ParseSceneSource(R"({
  format_version: 1,
  entities: [],
  instances: [
    { id: '0000000000000100', source: 'asset://props/door.sscene',
      entity_ids: { '0000000000000001': '0000000000000201',
                    '0000000000000002': '0000000000000202' },
      add: { '0000000000000001': { Shadow: { soft: true } } },
      remove: { '0000000000000001': ['Body'] },
      add_entities: [
        { id: '0000000000000210', parent_path: '0000000000000001',
          components: { Trigger: { radius: 2 } } },
      ],
      suppress: ['0000000000000002'] },
  ],
})", &error);
        ASSERT_TRUE(root.has_value()) << error;

        const auto result = ResolveSceneComposition(*root, sources, &error);
        ASSERT_TRUE(result.has_value()) << error;

        const ResolvedSceneEntity* body = FindEntity(*result, 0x201);
        ASSERT_NE(body, nullptr);
        EXPECT_NE(body->Components.Find("Shadow"), nullptr);
        EXPECT_EQ(body->Components.Find("Body"), nullptr);

        EXPECT_EQ(FindEntity(*result, 0x202), nullptr); // suppressed

        const ResolvedSceneEntity* trigger = FindEntity(*result, 0x210);
        ASSERT_NE(trigger, nullptr);
        EXPECT_EQ(trigger->Parent.Value, 0x201ull);
        EXPECT_EQ(trigger->Instance.Value, 0x100ull);
    }

    TEST(SceneComposition, SuppressingAParentDropsItsBranch)
    {
        MemorySources sources;
        sources.Add("asset://props/door.sscene", kDoorSource);

        std::string error;
        const auto root = ParseSceneSource(R"({
  format_version: 1,
  entities: [],
  instances: [
    { id: '0000000000000100', source: 'asset://props/door.sscene',
      entity_ids: { '0000000000000001': '0000000000000201',
                    '0000000000000002': '0000000000000202' },
      suppress: ['0000000000000001'] },
  ],
})", &error);
        ASSERT_TRUE(root.has_value()) << error;

        const auto result = ResolveSceneComposition(*root, sources, &error);
        ASSERT_TRUE(result.has_value()) << error;
        EXPECT_EQ(FindEntity(*result, 0x201), nullptr);
        EXPECT_EQ(FindEntity(*result, 0x202), nullptr); // the light went with it
        EXPECT_NE(FindEntity(*result, 0x100), nullptr); // the root remains
    }

    TEST(SceneComposition, MissingMintedIdsAreReportedAndTheirSubtreeSkipped)
    {
        MemorySources sources;
        sources.Add("asset://props/door.sscene", kDoorSource);

        std::string error;
        // Only the body has a recorded id; the light's path is unrecorded, as
        // it would be after the door source grew a new entity.
        const auto root = ParseSceneSource(R"({
  format_version: 1,
  entities: [],
  instances: [
    { id: '0000000000000100', source: 'asset://props/door.sscene',
      entity_ids: { '0000000000000001': '0000000000000201' } },
  ],
})", &error);
        ASSERT_TRUE(root.has_value()) << error;

        const auto result = ResolveSceneComposition(*root, sources, &error);
        ASSERT_TRUE(result.has_value()) << error;
        EXPECT_NE(FindEntity(*result, 0x201), nullptr);
        ASSERT_EQ(result->MissingIds.size(), 1u);
        EXPECT_EQ(result->MissingIds[0].first.Value, 0x100ull);
        EXPECT_EQ(result->MissingIds[0].second.ToString(), Hex(0x2));
    }

    TEST(SceneComposition, DanglingOverridesAreReportedNotFatal)
    {
        MemorySources sources;
        sources.Add("asset://props/door.sscene", kDoorSource);

        std::string error;
        const auto root = ParseSceneSource(R"({
  format_version: 1,
  entities: [],
  instances: [
    { id: '0000000000000100', source: 'asset://props/door.sscene',
      entity_ids: { '0000000000000001': '0000000000000201',
                    '0000000000000002': '0000000000000202' },
      patch: { '00000000000000ff': { Body: { hp: 1 } } } },
  ],
})", &error);
        ASSERT_TRUE(root.has_value()) << error;

        const auto result = ResolveSceneComposition(*root, sources, &error);
        ASSERT_TRUE(result.has_value()) << error;
        ASSERT_EQ(result->DanglingOverrides.size(), 1u);
        EXPECT_NE(result->DanglingOverrides[0].find(Hex(0xff)), std::string::npos);
        EXPECT_EQ(result->Entities.size(), 3u); // resolution itself unharmed
    }

    TEST(SceneComposition, UnresolvableSourcesAndCyclesFail)
    {
        MemorySources sources;
        std::string error;
        const auto missing = ParseSceneSource(R"({
  format_version: 1, entities: [],
  instances: [ { id: '0000000000000100', source: 'asset://gone.sscene' } ],
})", &error);
        ASSERT_TRUE(missing.has_value()) << error;
        EXPECT_FALSE(ResolveSceneComposition(*missing, sources, &error).has_value());
        EXPECT_NE(error.find("did not resolve"), std::string::npos);

        sources.Add("asset://a.sscene", R"({
  format_version: 1, entities: [],
  instances: [ { id: '0000000000000100', source: 'asset://b.sscene' } ],
})");
        sources.Add("asset://b.sscene", R"({
  format_version: 1, entities: [],
  instances: [ { id: '0000000000000101', source: 'asset://a.sscene' } ],
})");
        const SceneSourceDocument* a = sources.Find("asset://a.sscene");
        ASSERT_NE(a, nullptr);
        EXPECT_FALSE(ResolveSceneComposition(*a, sources, &error).has_value());
        EXPECT_NE(error.find("cycle"), std::string::npos);
    }
} // namespace
