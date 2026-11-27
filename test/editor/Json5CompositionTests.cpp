#include "CompositionRoundTrip.h"

#include "scene_source/Json5Composition.h"
#include "scene_source/Json5Parser.h"
#include "scene_source/Json5Writer.h"

#include <core/composition/CompositionPatch.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
Json5Value Parse(std::string_view text)
{
    Json5ParseError error;
    std::optional<Json5Value> value = Json5Parse(text, &error);
    EXPECT_TRUE(value.has_value()) << error.Message << ": " << text;
    return value.value_or(Json5Value{});
}

std::string Codes(const CompositionReport& report)
{
    std::string codes;
    for (const CompositionDiagnostic& diagnostic : report.Diagnostics)
        codes += diagnostic.Code + " at " + diagnostic.Path + "; ";
    return codes;
}
} // namespace

TEST(Json5Composition, ObjectsEqualWhateverTheirMembersOrder)
{
    EXPECT_TRUE(Parse("{ a: 1, b: { c: 2, d: [1, { e: 3, f: 4 }] } }")
                    .SameValueAs(Parse("{ b: { d: [1, { f: 4, e: 3 }], c: 2 }, a: 1 }")));
    EXPECT_FALSE(Parse("{ a: 1, b: 2 }").SameValueAs(Parse("{ a: 1, c: 2 }")));
    EXPECT_FALSE(Parse("{ a: 1 }").SameValueAs(Parse("{ a: 1, b: 2 }")));
    EXPECT_FALSE(Parse("[1, 2]").SameValueAs(Parse("[2, 1]"))) << "a list's order is part of its value";
}

TEST(Json5Composition, FragmentsWhoseListedRecordsOrderTheirMembersDifferentlyAgree)
{
    const Json5Value base = Parse("{ req: 1 }");
    const Json5Value fire = Parse("{ rows: [ { x: 1, y: 2 } ] }");
    const Json5Value frost = Parse("{ rows: [ { y: 2, x: 1 } ] }");
    const Json5Value local = Json5Value::MakeObject();
    const DataFieldSchema schema = CompositionRoundTrip::Schema();
    const CompositionLayerInput<Json5Value> baseLayer{ &base, "base", nullptr };
    const std::vector<CompositionLayerInput<Json5Value>> includes{ { &fire, "fire", nullptr }, { &frost, "frost", nullptr } };
    CompositionReport report;
    (void)ComposeLayers<Json5Value>(&baseLayer, includes, { &local, "child", nullptr },
                                    { .Schema = &schema }, report);
    EXPECT_TRUE(report.Diagnostics.empty()) << Codes(report);
}

TEST(Json5Composition, EveryTargetComposesBackFromItsPatch)
{
    const DataFieldSchema schema = CompositionRoundTrip::Schema();
    CompositionRoundTrip::Generator generator(20261119u);
    for (int i = 0; i < 2000; ++i)
    {
        const CompositionRoundTrip::Case pair = generator.Next();
        const Json5Value base = Parse(pair.Base);
        const Json5Value target = Parse(pair.Target);
        const std::optional<Json5Value> patch = PatchFrom(base, target, &schema);
        const Json5Value nothing = Json5Value::MakeObject();
        const CompositionLayerInput<Json5Value> baseLayer{ &base, "base", nullptr };
        CompositionReport report;
        const Json5Value composed = ComposeLayers<Json5Value>(&baseLayer, {}, { patch ? &*patch : &nothing, "save", nullptr },
                                                              { .Schema = &schema }, report);
        ASSERT_TRUE(report.Diagnostics.empty()) << Codes(report) << "\nbase   " << pair.Base << "\ntarget " << pair.Target;
        ASSERT_TRUE(composed.SameValueAs(target))
            << "base   " << pair.Base << "\ntarget " << pair.Target << "\ngot    " << Json5Write(composed);
    }
}
