// The .sscene typed model: stable-id hierarchy round-trip, instance records
// with minted ids and overrides, the validations one file can perform on
// itself, retention of what this build does not know, and the hard-cutover
// diagnostic for the retired format.

#include "scene_source/SceneSourceDocument.h"

#include <gtest/gtest.h>

#include <string>

namespace
{
    [[nodiscard]] SceneSourceDocument ParseOk(std::string_view text)
    {
        std::string error;
        std::optional<SceneSourceDocument> parsed = ParseSceneSource(text, &error);
        EXPECT_TRUE(parsed.has_value()) << error;
        return parsed.has_value() ? std::move(*parsed) : SceneSourceDocument{};
    }

    [[nodiscard]] std::string ParseFailure(std::string_view text)
    {
        std::string error;
        EXPECT_FALSE(ParseSceneSource(text, &error).has_value()) << "accepted:\n" << text;
        return error;
    }

    constexpr std::string_view kTwoEntityScene = R"({
  format_version: 1,
  settings: { default_material: 'asset://materials/dev/gray.smat' },
  entities: [
    // the floor
    { id: '00000000000000aa',
      components: { Transform: { local: { position: [0, -0.25, 0] } } } },
    { id: '00000000000000bb', parent: '00000000000000aa', hidden: true,
      components: { Transform: { local: { position: [0, 1, 0] } } } },
  ],
})";

    TEST(SceneSourceDocument, ParsesStableIdHierarchy)
    {
        const SceneSourceDocument doc = ParseOk(kTwoEntityScene);
        ASSERT_EQ(doc.Entities.size(), 2u);
        EXPECT_EQ(doc.Entities[0].Id.Value, 0xaaull);
        EXPECT_FALSE(doc.Entities[0].Parent.IsValid());
        EXPECT_EQ(doc.Entities[1].Parent.Value, 0xaaull);
        EXPECT_TRUE(doc.Entities[1].Hidden);
        EXPECT_FALSE(doc.Entities[1].Locked);
        EXPECT_NE(doc.Entities[0].Components.Find("Transform"), nullptr);
        EXPECT_NE(doc.Settings.Find("default_material"), nullptr);
    }

    TEST(SceneSourceDocument, RoundTripsAndSavesTwiceByteIdentically)
    {
        const SceneSourceDocument doc = ParseOk(kTwoEntityScene);
        const std::string once = WriteSceneSource(doc);
        const SceneSourceDocument reparsed = ParseOk(once);
        const std::string twice = WriteSceneSource(reparsed);
        EXPECT_EQ(once, twice);

        // Semantics held: same records, and the record comment survived.
        ASSERT_EQ(reparsed.Entities.size(), 2u);
        EXPECT_EQ(reparsed.Entities[1].Parent.Value, 0xaaull);
        ASSERT_EQ(reparsed.Entities[0].LeadingComments.size(), 1u);
        EXPECT_EQ(reparsed.Entities[0].LeadingComments[0], "// the floor");
    }

    TEST(SceneSourceDocument, InstanceRecordsRoundTrip)
    {
        const SceneSourceDocument doc = ParseOk(R"({
  format_version: 1,
  entities: [ { id: '00000000000000aa', components: {} } ],
  instances: [
    {
      id: '0000000000000100',
      parent: '00000000000000aa',
      source: 'asset://props/door.sscene',
      transform: { position: [5, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
      entity_ids: { '0000000000000001': '0000000000000200',
                    '0000000000000002': '0000000000000201' },
      patch: { '0000000000000001': { PointLight: { intensity: 40 } } },
      add: { '0000000000000002': { AudioSource: { clip: 'asset://sfx/creak.sclip' } } },
      remove: { '0000000000000002': ['Shadow'] },
      add_entities: [
        { id: '0000000000000202', parent_path: '0000000000000001',
          components: { Transform: {} } },
      ],
      suppress: ['0000000000000003'],
    },
  ],
})");
        ASSERT_EQ(doc.Instances.size(), 1u);
        const SceneInstanceRecord& instance = doc.Instances[0];
        EXPECT_EQ(instance.Id.Value, 0x100ull);
        EXPECT_EQ(instance.Parent.Value, 0xaaull);
        EXPECT_EQ(instance.Source, "asset://props/door.sscene");
        EXPECT_EQ(instance.Placement.Position.X, 5.0f);
        ASSERT_EQ(instance.EntityIds.size(), 2u);
        EXPECT_EQ(instance.EntityIds[0].second.Value, 0x200ull);
        ASSERT_EQ(instance.Patches.size(), 1u);
        EXPECT_NE(instance.Patches[0].second.Find("PointLight"), nullptr);
        ASSERT_EQ(instance.RemovedComponents.size(), 1u);
        EXPECT_EQ(instance.RemovedComponents[0].second[0], "Shadow");
        ASSERT_EQ(instance.AddedEntities.size(), 1u);
        EXPECT_EQ(instance.AddedEntities[0].Id.Value, 0x202ull);
        ASSERT_EQ(instance.Suppressed.size(), 1u);
        EXPECT_EQ(instance.Suppressed[0].Elements[0], 3ull);

        const std::string once = WriteSceneSource(doc);
        const std::string twice = WriteSceneSource(ParseOk(once));
        EXPECT_EQ(once, twice);
    }

    TEST(SceneSourceDocument, RetainsUnknownRootMembersAndComponents)
    {
        const SceneSourceDocument doc = ParseOk(R"({
  format_version: 1,
  entities: [
    { id: '00000000000000aa',
      components: {
        Transform: { local: { position: [0, 0, 0] } },
        FutureGameComponent: { charge: 7, mode: 'overdrive' },
      } },
  ],
  future_root_feature: { enabled: true },
})");
        ASSERT_EQ(doc.UnknownRoot.size(), 1u);
        EXPECT_EQ(doc.UnknownRoot[0].first, "future_root_feature");

        const std::string written = WriteSceneSource(doc);
        EXPECT_NE(written.find("future_root_feature"), std::string::npos);
        EXPECT_NE(written.find("FutureGameComponent"), std::string::npos);
        EXPECT_NE(written.find("overdrive"), std::string::npos);
    }

    TEST(SceneSourceDocument, RejectsTheRetiredFormatByName)
    {
        const std::string error = ParseFailure(
            R"({ "version": 1, "entities": [ { "components": {} } ], "hierarchy": [] })");
        EXPECT_NE(error.find(".level.json"), std::string::npos) << error;
        EXPECT_NE(error.find(".sscene"), std::string::npos) << error;
    }

    TEST(SceneSourceDocument, RejectsIdentityViolations)
    {
        // Duplicate id across records.
        EXPECT_NE(ParseFailure(R"({ format_version: 1, entities: [
            { id: '00000000000000aa', components: {} },
            { id: '00000000000000aa', components: {} } ] })")
                      .find("already used"), std::string::npos);
        // A minted instance id colliding with a local entity.
        EXPECT_NE(ParseFailure(R"({ format_version: 1,
            entities: [ { id: '00000000000000aa', components: {} } ],
            instances: [ { id: '0000000000000100', source: 'asset://a.sscene',
                           entity_ids: { '0000000000000001': '00000000000000aa' } } ] })")
                      .find("already used"), std::string::npos);
        // Runtime-namespace id.
        EXPECT_NE(ParseFailure(R"({ format_version: 1, entities: [
            { id: '8000000000000001', components: {} } ] })")
                      .find("runtime namespace"), std::string::npos);
    }

    TEST(SceneSourceDocument, RejectsParentViolations)
    {
        EXPECT_NE(ParseFailure(R"({ format_version: 1, entities: [
            { id: '00000000000000aa', parent: '00000000000000ff', components: {} } ] })")
                      .find("does not exist"), std::string::npos);
        EXPECT_NE(ParseFailure(R"({ format_version: 1, entities: [
            { id: '00000000000000aa', parent: '00000000000000aa', components: {} } ] })")
                      .find("parents itself"), std::string::npos);
        EXPECT_NE(ParseFailure(R"({ format_version: 1, entities: [
            { id: '00000000000000aa', parent: '00000000000000bb', components: {} },
            { id: '00000000000000bb', parent: '00000000000000aa', components: {} } ] })")
                      .find("cycle"), std::string::npos);
    }

    TEST(SceneSourceDocument, RejectsContradictoryOverrides)
    {
        const std::string error = ParseFailure(R"({ format_version: 1, entities: [],
            instances: [ { id: '0000000000000100', source: 'asset://a.sscene',
                add:    { '0000000000000001': { Shadow: {} } },
                remove: { '0000000000000001': ['Shadow'] } } ] })");
        EXPECT_NE(error.find("both added and removed"), std::string::npos) << error;
    }

    TEST(SceneSourceDocument, RejectsMalformedInstanceRecords)
    {
        EXPECT_NE(ParseFailure(R"({ format_version: 1, entities: [],
            instances: [ { id: '0000000000000100', source: 'props/door.sscene' } ] })")
                      .find("asset://"), std::string::npos);
        EXPECT_NE(ParseFailure(R"({ format_version: 1, entities: [],
            instances: [ { id: '0000000000000100', source: 'asset://door.smesh' } ] })")
                      .find("asset://"), std::string::npos);
        EXPECT_NE(ParseFailure(R"({ format_version: 1, entities: [],
            instances: [ { id: '0000000000000100', source: 'asset://a.sscene',
                           entity_ids: { 'not-a-path': '0000000000000200' } } ] })")
                      .find("path"), std::string::npos);
        EXPECT_NE(ParseFailure(R"({ format_version: 1, entities: [],
            instances: [ { id: '0000000000000100', source: 'asset://a.sscene',
                           transform: { position: [1, 2] } } ] })")
                      .find("3 numbers"), std::string::npos);
    }

    TEST(SceneSourceDocument, ElementPathsRoundTripAsText)
    {
        SceneElementPath path;
        path.Elements = { 0x100, 0x1 };
        const std::string text = path.ToString();
        EXPECT_EQ(text, "0000000000000100/0000000000000001");
        const std::optional<SceneElementPath> back = SceneElementPath::FromString(text);
        ASSERT_TRUE(back.has_value());
        EXPECT_EQ(*back, path);
        EXPECT_FALSE(SceneElementPath::FromString("").has_value());
        EXPECT_FALSE(SceneElementPath::FromString("0000000000000100/").has_value());
        EXPECT_FALSE(SceneElementPath::FromString("zz").has_value());
    }
} // namespace
