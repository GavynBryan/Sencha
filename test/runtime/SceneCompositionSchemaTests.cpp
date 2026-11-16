// A component whose scene form holds a keyed list declares the key, so a
// prefab patches one element by key instead of replacing the list
// (docs/plans/composition-stage2-prefab-extends.md).

#include <gtest/gtest.h>

#include <abilities/AbilityGrantsSerializer.h>
#include <attributes/AttributeSetSerializer.h>
#include <core/composition/JsonComposition.h>
#include <core/json/JsonEquals.h>
#include <core/json/JsonParser.h>
#include <gameplay_tags/GameplayTagContainerSerializer.h>

#include <memory>
#include <string_view>

namespace
{
    JsonValue Compose(const IComponentSerializer& serializer, std::string_view base, std::string_view patch)
    {
        JsonValue target = *JsonParse(base);
        CompositionReport report;
        ApplyCompositionPatch<JsonValue>(target, *JsonParse(patch), "asset://prefabs/derived.sscene",
                                         { .Schema = serializer.CompositionSchema(), .Null = CompositionNull::Removes },
                                         report);
        EXPECT_TRUE(report.Diagnostics.empty()) << report.Diagnostics.front().Code << " " << report.Diagnostics.front().Path;
        return target;
    }
}

TEST(SceneCompositionSchema, AbilityGrantsMergeByKey)
{
    const std::unique_ptr<IComponentSerializer> grants = MakeAbilityGrantsSerializer();
    ASSERT_NE(grants->CompositionSchema(), nullptr);
    const JsonValue composed = Compose(*grants,
        R"({ "grants": [ { "key": "g_a", "ability": "asset://abilities/a.sdata" },
                         { "key": "g_b", "ability": "asset://abilities/b.sdata" } ] })",
        R"({ "grants": { "g_b": null, "g_c": { "ability": "asset://abilities/c.sdata" } } })");
    EXPECT_TRUE(JsonEquals(composed, *JsonParse(R"({ "grants": [
        { "key": "g_a", "ability": "asset://abilities/a.sdata" },
        { "key": "g_c", "ability": "asset://abilities/c.sdata" } ] })")));
}

TEST(SceneCompositionSchema, AttributesMergeByName)
{
    const std::unique_ptr<IComponentSerializer> attributes = MakeAttributeSetSerializer();
    ASSERT_NE(attributes->CompositionSchema(), nullptr);
    const JsonValue composed = Compose(*attributes,
        R"({ "attributes": [ { "name": "Health", "base": 100 }, { "name": "Stealth", "base": 5 } ] })",
        R"({ "attributes": { "Health": { "base": 200 }, "Stealth": null } })");
    EXPECT_TRUE(JsonEquals(composed, *JsonParse(R"({ "attributes": [ { "name": "Health", "base": 200 } ] })")));
}

TEST(SceneCompositionSchema, GameplayTagsMergeByName)
{
    const std::unique_ptr<IComponentSerializer> tags = MakeGameplayTagContainerSerializer();
    ASSERT_NE(tags->CompositionSchema(), nullptr);
    const JsonValue composed = Compose(*tags,
        R"({ "tags": [ { "name": "Creature.Humanoid", "stacks": 1 } ] })",
        R"({ "tags": { "Creature.Humanoid.Guard": { "stacks": 1 } } })");
    EXPECT_TRUE(JsonEquals(composed, *JsonParse(R"({ "tags": [ { "name": "Creature.Humanoid", "stacks": 1 },
        { "name": "Creature.Humanoid.Guard", "stacks": 1 } ] })")));
}
