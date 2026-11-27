#include "scene_source/SceneSourceDocument.h"
#include "scene_source/SceneSourceUpgrade.h"

#include <gtest/gtest.h>

#include <string>

namespace
{
    [[nodiscard]] SceneSourceUpgrade UpgradeOk(std::string_view text)
    {
        std::string error;
        std::optional<SceneSourceUpgrade> upgrade = UpgradeSceneSourceText(text, &error);
        EXPECT_TRUE(upgrade.has_value()) << error;
        return upgrade.has_value() ? std::move(*upgrade) : SceneSourceUpgrade{};
    }

    [[nodiscard]] SceneSourceDocument ReadsAtThree(const std::string& text)
    {
        std::string error;
        std::optional<SceneSourceDocument> parsed = ParseSceneSource(text, &error);
        EXPECT_TRUE(parsed.has_value()) << error << "\n" << text;
        return parsed.has_value() ? std::move(*parsed) : SceneSourceDocument{};
    }

    [[nodiscard]] std::string Joined(const std::vector<std::string>& lines)
    {
        std::string out;
        for (const std::string& line : lines)
            out += line + "\n";
        return out;
    }

    [[nodiscard]] std::string WithVersion(std::string text, std::string_view from, std::string_view to)
    {
        const std::size_t at = text.find(from);
        EXPECT_NE(at, std::string::npos) << text;
        if (at != std::string::npos)
            text.replace(at, from.size(), to);
        return text;
    }

    constexpr std::string_view kVersionOnePlacement = R"({
  format_version: 1,
  entities: [],
  instances: [
    {
      id: "0000000000000100",
      source: "asset://prefabs/humanoid.sscene",
      entity_ids: { "00000000000000a1": "0000000000000101" },
      patch: {
        "00000000000000a1": {
          Attributes: { attributes: [{ name: "Health", base: 150 }] },
          responses: { bindings: { struck: { cooldown: 2 } } },
        },
      },
    },
  ],
}
)";

    TEST(SceneSourceUpgrade, AVersionOnePlacementPatchIsKeptAsItIs)
    {
        const SceneSourceUpgrade upgrade = UpgradeOk(kVersionOnePlacement);
        EXPECT_TRUE(upgrade.Refused.empty()) << Joined(upgrade.Refused);
        EXPECT_FALSE(upgrade.Changed.empty());
        EXPECT_EQ(upgrade.Text, WithVersion(std::string(kVersionOnePlacement), "format_version: 1", "format_version: 3"));
        const SceneSourceDocument document = ReadsAtThree(upgrade.Text);
        ASSERT_EQ(document.Instances.size(), 1u);
        ASSERT_EQ(document.Instances[0].Patches.size(), 1u);
        EXPECT_NE(document.Instances[0].Patches[0].second.Find("Attributes"), nullptr);
        EXPECT_NE(document.Instances[0].Patches[0].second.Find("responses"), nullptr);
    }

    TEST(SceneSourceUpgrade, AVersionOneNullInsideAPlacementPatchIsRefusedAndNamed)
    {
        constexpr std::string_view kText = R"({
  format_version: 1,
  entities: [],
  instances: [
    { id: '0000000000000100', source: 'asset://props/door.sscene',
      entity_ids: { '0000000000000001': '0000000000000200' },
      patch: { '0000000000000001': { PointLight: { intensity: 40, cookie: null } } } },
  ],
})";
        const SceneSourceUpgrade upgrade = UpgradeOk(kText);
        ASSERT_EQ(upgrade.Refused.size(), 1u) << Joined(upgrade.Refused);
        EXPECT_NE(upgrade.Refused[0].find("0000000000000100"), std::string::npos) << upgrade.Refused[0];
        EXPECT_NE(upgrade.Refused[0].find("0000000000000001.PointLight.cookie"), std::string::npos) << upgrade.Refused[0];
        EXPECT_EQ(upgrade.Text, kText);
    }

    TEST(SceneSourceUpgrade, AVersionOneComponentNullStaysARemoval)
    {
        const SceneSourceUpgrade upgrade = UpgradeOk(R"({ format_version: 1, entities: [],
  instances: [ { id: '0000000000000100', source: 'asset://props/door.sscene',
    patch: { '0000000000000001': { Shadow: null } } } ] })");
        EXPECT_TRUE(upgrade.Refused.empty()) << Joined(upgrade.Refused);
        const SceneSourceDocument document = ReadsAtThree(upgrade.Text);
        ASSERT_EQ(document.Instances.size(), 1u);
        ASSERT_EQ(document.Instances[0].Patches.size(), 1u);
        ASSERT_NE(document.Instances[0].Patches[0].second.Find("Shadow"), nullptr);
        EXPECT_TRUE(document.Instances[0].Patches[0].second.Find("Shadow")->IsNull());
    }

    TEST(SceneSourceUpgrade, VersionTwoAddAndRemoveAreFoldedIntoPatch)
    {
        const SceneSourceUpgrade upgrade = UpgradeOk(R"({
  format_version: 2,
  extends: 'asset://prefabs/humanoid.sscene',
  patch: { '00000000000000a1': { Attributes: { attributes: { Health: { base: 200 } } } } },
  remove: { '00000000000000a1': ['LookOrientation'], '00000000000000a2': ['Body'] },
  instances: [
    { id: '0000000000000100', source: 'asset://props/door.sscene',
      entity_ids: { '0000000000000001': '0000000000000200', '0000000000000002': '0000000000000201' },
      patch: { '0000000000000001': { PointLight: { intensity: 40 } } },
      add: { '0000000000000002': { AudioSource: { clip: 'asset://sfx/creak.sclip' } } },
      remove: { '0000000000000001': ['Shadow'], '0000000000000002': ['Body'] } },
  ],
})");
        EXPECT_TRUE(upgrade.Refused.empty()) << Joined(upgrade.Refused);
        EXPECT_EQ(upgrade.Text.find("add:"), std::string::npos) << upgrade.Text;
        EXPECT_EQ(upgrade.Text.find("remove:"), std::string::npos) << upgrade.Text;
        EXPECT_GE(upgrade.Changed.size(), 4u) << Joined(upgrade.Changed);

        const SceneSourceDocument document = ReadsAtThree(upgrade.Text);
        ASSERT_EQ(document.InheritedPatches.size(), 2u);
        EXPECT_NE(document.InheritedPatches[0].second.Find("Attributes"), nullptr);
        EXPECT_TRUE(document.InheritedPatches[0].second.Find("LookOrientation")->IsNull());
        EXPECT_EQ(document.InheritedPatches[1].first.Value, 0xa2ull);
        EXPECT_TRUE(document.InheritedPatches[1].second.Find("Body")->IsNull());

        const SceneInstanceRecord& instance = document.Instances[0];
        ASSERT_EQ(instance.Patches.size(), 2u);
        EXPECT_NE(instance.Patches[0].second.Find("PointLight"), nullptr);
        EXPECT_TRUE(instance.Patches[0].second.Find("Shadow")->IsNull());
        ASSERT_NE(instance.Patches[1].second.Find("AudioSource"), nullptr);
        EXPECT_EQ(instance.Patches[1].second.Find("AudioSource")->Find("clip")->Text, "asset://sfx/creak.sclip");
        EXPECT_TRUE(instance.Patches[1].second.Find("Body")->IsNull());
    }

    TEST(SceneSourceUpgrade, ANameBothAddedAndPatchedIsRefusedAndNamed)
    {
        constexpr std::string_view kText = R"({ format_version: 2, entities: [],
  instances: [ { id: '0000000000000100', source: 'asset://a.sscene',
    patch: { '0000000000000001': { Shadow: { soft: true } } },
    add:   { '0000000000000001': { Shadow: {} } } } ] })";
        const SceneSourceUpgrade upgrade = UpgradeOk(kText);
        ASSERT_EQ(upgrade.Refused.size(), 1u) << Joined(upgrade.Refused);
        EXPECT_NE(upgrade.Refused[0].find("Shadow"), std::string::npos) << upgrade.Refused[0];
        EXPECT_NE(upgrade.Refused[0].find("0000000000000001"), std::string::npos) << upgrade.Refused[0];
        EXPECT_NE(upgrade.Refused[0].find("both patched and added"), std::string::npos) << upgrade.Refused[0];
        EXPECT_EQ(upgrade.Text, kText);
    }

    TEST(SceneSourceUpgrade, ANameBothStatedAndRemovedIsRefused)
    {
        EXPECT_NE(Joined(UpgradeOk(R"({ format_version: 1, entities: [],
  instances: [ { id: '0000000000000100', source: 'asset://a.sscene',
    add:    { '0000000000000001': { Shadow: {} } },
    remove: { '0000000000000001': ['Shadow'] } } ] })").Refused).find("both added and removed"),
                  std::string::npos);
        EXPECT_NE(Joined(UpgradeOk(R"({ format_version: 2, entities: [],
  patch: { '00000000000000a1': { Shadow: { soft: true } } },
  remove: { '00000000000000a1': ['Shadow'] } })").Refused).find("both patched and removed"),
                  std::string::npos);
    }

    TEST(SceneSourceUpgrade, AVersionOneFileCannotUseVersionTwoMembers)
    {
        const SceneSourceUpgrade upgrade = UpgradeOk("{ format_version: 1, extends: 'asset://prefabs/a.sscene', entities: [] }");
        ASSERT_EQ(upgrade.Refused.size(), 1u) << Joined(upgrade.Refused);
        EXPECT_NE(upgrade.Refused[0].find("extends"), std::string::npos) << upgrade.Refused[0];
    }

    TEST(SceneSourceUpgrade, UpgradingTwiceChangesNothingTheSecondTime)
    {
        for (const std::string_view text :
             { kVersionOnePlacement,
               std::string_view(R"({ format_version: 2, entities: [],
  instances: [ { id: '0000000000000100', source: 'asset://a.sscene',
    add: { '0000000000000001': { Shadow: { soft: true } } },
    remove: { '0000000000000001': ['Body'] } } ] })") })
        {
            const SceneSourceUpgrade once = UpgradeOk(text);
            const SceneSourceUpgrade twice = UpgradeOk(once.Text);
            EXPECT_TRUE(twice.Changed.empty()) << Joined(twice.Changed);
            EXPECT_TRUE(twice.Refused.empty()) << Joined(twice.Refused);
            EXPECT_EQ(twice.Text, once.Text);
        }
    }

    TEST(SceneSourceUpgrade, APlainFileChangesOnlyItsVersion)
    {
        constexpr std::string_view kJson = R"({
  "id": "c3dd45711f65f6eb",
  "format_version": 1,
  "entities": [
    {
      "id": "11c04b1a77e0a001",
      "components": {
        "name": {
          "value": "Player Pawn"
        }
      }
    }
  ]
}
)";
        EXPECT_EQ(UpgradeOk(kJson).Text, WithVersion(std::string(kJson), "\"format_version\": 1", "\"format_version\": 3"));

        constexpr std::string_view kJson5 = R"(// the box room
{
  format_version: 2,
  settings: {default_material: 'asset://materials/dev/gray.smat'},
  entities: [
    // the floor
    { id: '00000000000000aa', components: { Transform: { local: { position: [0, -0.25, 0] } } } },
  ],
}
)";
        const SceneSourceUpgrade upgrade = UpgradeOk(kJson5);
        EXPECT_EQ(upgrade.Text, WithVersion(std::string(kJson5), "format_version: 2", "format_version: 3"));
        EXPECT_EQ(upgrade.Changed, std::vector<std::string>{ "format_version 2 -> 3" });
    }

    TEST(SceneSourceUpgrade, AVersionThreeFileIsCurrent)
    {
        constexpr std::string_view kText = "{ format_version: 3, entities: [] }";
        const SceneSourceUpgrade upgrade = UpgradeOk(kText);
        EXPECT_TRUE(upgrade.Changed.empty());
        EXPECT_TRUE(upgrade.Refused.empty());
        EXPECT_EQ(upgrade.Text, kText);
    }

    TEST(SceneSourceUpgrade, WhatIsNotAKnownSceneSourceIsNotUpgraded)
    {
        for (const std::string_view text : { std::string_view("{ entities: [] }"), std::string_view("{ format_version: 4 }"),
                                             std::string_view("[ 1, 2 ]"), std::string_view("{ format_version: ") })
        {
            std::string error;
            EXPECT_FALSE(UpgradeSceneSourceText(text, &error).has_value()) << text;
            EXPECT_FALSE(error.empty()) << text;
        }
    }
} // namespace
