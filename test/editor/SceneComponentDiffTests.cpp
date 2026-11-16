// What an edited entity is written as, against what it inherits: the sparsest
// patch that composes back to it, keyed lists by key with removed elements as
// null, so a later change to anything it did not touch still reaches it.

#include "scene_source/Json5Composition.h"
#include "scene_source/Json5Parser.h"
#include "scene_source/Json5Writer.h"
#include "scene_source/SceneComponentDiff.h"

#include <core/metadata/DataSchema.h>

#include <gtest/gtest.h>

#include <map>
#include <string>

namespace
{
Json5Value Parse(std::string_view text)
{
    Json5ParseError error;
    std::optional<Json5Value> value = Json5Parse(text, &error);
    EXPECT_TRUE(value.has_value()) << error.Message;
    return value.value_or(Json5Value{});
}

struct Schemas
{
    DataFieldSchema Attributes = MakeKeyedListRecordSchema("attributes", "name");

    [[nodiscard]] SceneComponentSchemaFn Fn() const
    {
        return [this](std::string_view component) -> const DataFieldSchema* {
            return component == "Attributes" ? &Attributes : nullptr;
        };
    }
};
} // namespace

TEST(SceneComponentDiff, AKeyedListIsWrittenByKeyWithRemovedElementsAsNull)
{
    const Schemas schemas;
    const Json5Value baseline = Parse(R"({
        Attributes: { attributes: [ { name: 'Health', base: 100 }, { name: 'Stealth', base: 5 } ] },
        Light: { intensity: 5, range: 3 } })");
    const Json5Value live = Parse(R"({
        Attributes: { attributes: [ { name: 'Health', base: 200 }, { name: 'Speed', base: 4 } ] },
        Light: { intensity: 5, range: 3 } })");

    const SceneComponentDiff diff = DiffSceneComponents(live, baseline, schemas.Fn());
    EXPECT_TRUE(diff.Removed.empty());
    const Json5Value expected = Parse(R"({ Attributes: { attributes: {
        Health: { base: 200 }, Speed: { name: 'Speed', base: 4 }, Stealth: null } } })");
    EXPECT_TRUE(diff.Patch.SameValueAs(expected)) << Json5Write(diff.Patch);

    Json5Value recomposed = baseline;
    CompositionReport report;
    DataFieldSchema entity = MakeDataField(DataFieldKind::Record, {}, {});
    DataFieldSchema attributes = schemas.Attributes;
    attributes.Key = "Attributes";
    entity.Children.push_back(attributes);
    ApplyCompositionPatch(recomposed, diff.Patch, "", { .Schema = &entity, .Null = CompositionNull::Removes }, report);
    EXPECT_TRUE(report.Diagnostics.empty());
    EXPECT_TRUE(recomposed.SameValueAs(live)) << "the patch composes back to the edited entity";
}

TEST(SceneComponentDiff, AnUndescribedListIsWholeAndAComponentGoneIsRemoved)
{
    const Schemas schemas;
    const SceneComponentDiff diff = DiffSceneComponents(
        Parse("{ Emitter: { colors: [1, 2], label: 'x' }, Light: { intensity: 5 }, brush: { id: 3 } }"),
        Parse("{ Emitter: { colors: [1], label: 'x' }, Light: { intensity: 5 }, Body: { mass: 1 }, brush: { id: 9 } }"),
        schemas.Fn());
    EXPECT_TRUE(diff.Patch.SameValueAs(Parse("{ Emitter: { colors: [1, 2] } }"))) << Json5Write(diff.Patch);
    EXPECT_EQ(diff.Removed, std::vector<std::string>{ "Body" });
}

TEST(SceneComponentDiff, NothingChangedIsNothing)
{
    const Schemas schemas;
    const Json5Value same = Parse("{ Attributes: { attributes: [ { name: 'Health', base: 1 } ] } }");
    const SceneComponentDiff diff = DiffSceneComponents(same, same, schemas.Fn());
    EXPECT_TRUE(diff.Patch.Members.empty());
    EXPECT_TRUE(diff.Removed.empty());
}

// A member cleared inside a component is written as its removal, so the
// cleared value is not composed back from the base.
TEST(SceneComponentDiff, AMemberClearedInsideAComponentIsWrittenAsNull)
{
    const Schemas schemas;
    const Json5Value baseline = Parse(R"({ Light: { intensity: 5, range: 3, falloff: { curve: 'smooth' } } })");
    const Json5Value live = Parse(R"({ Light: { intensity: 5, falloff: {} } })");

    const SceneComponentDiff diff = DiffSceneComponents(live, baseline, schemas.Fn());
    const Json5Value expected = Parse(R"({ Light: { falloff: { curve: null }, range: null } })");
    EXPECT_TRUE(diff.Patch.SameValueAs(expected)) << Json5Write(diff.Patch);
    EXPECT_TRUE(DiffSceneComponents(baseline, baseline, schemas.Fn()).Patch.Members.empty());
}

// A response binding deleted from a placed or inherited entity is written as
// its removal, which a version-2 patch applies.
TEST(SceneComponentDiff, ADeletedResponseBindingIsWrittenAsNull)
{
    const Schemas schemas;
    const Json5Value baseline = Parse(R"({ responses: { bindings: {
        shot: { on: 'game.struck', sequence: { conditions: [], commands: [] } },
        use: { on: 'game.used', sequence: { conditions: [], commands: [] } } } } })");
    const Json5Value live = Parse(R"({ responses: { bindings: {
        use: { on: 'game.used', sequence: { conditions: [], commands: [] } } } } })");

    const SceneComponentDiff diff = DiffSceneComponents(live, baseline, schemas.Fn());
    EXPECT_TRUE(diff.Patch.SameValueAs(Parse("{ responses: { bindings: { shot: null } } }"))) << Json5Write(diff.Patch);

    Json5Value composed = baseline;
    CompositionReport report;
    ApplyCompositionPatch(composed, diff.Patch, "asset://levels/hub.sscene",
                          { .Schema = nullptr, .Null = CompositionNull::Removes }, report);
    EXPECT_TRUE(composed.SameValueAs(live)) << Json5Write(composed);
}
