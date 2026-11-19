#include "CompositionRoundTrip.h"

#include <core/composition/CompositionPatch.h>
#include <core/composition/JsonComposition.h>
#include <core/json/JsonEquals.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/metadata/DataSchema.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    DataFieldSchema Member(DataFieldKind kind, std::string key, bool required = false)
    {
        DataFieldSchema field = MakeDataField(kind, std::move(key), "");
        field.Required = required;
        return field;
    }

    DataFieldSchema Record(std::string key, std::vector<DataFieldSchema> members, bool required = false)
    {
        DataFieldSchema record = Member(DataFieldKind::Record, std::move(key), required);
        record.Children = std::move(members);
        return record;
    }

    DataFieldSchema List(std::string key, DataFieldSchema element)
    {
        DataFieldSchema list = Member(DataFieldKind::Array, std::move(key));
        list.Children.push_back(std::move(element));
        return list;
    }

    DataFieldSchema DefinitionSchema()
    {
        DataFieldSchema facts = List("facts", Record("", { Member(DataFieldKind::String, "name", true),
                                                           Member(DataFieldKind::String, "kind") }));
        facts.MergeKey = "name";
        facts.MergePolicy = DataMergePolicy::AppendOnly;
        return Record("", {
            Member(DataFieldKind::Float, "req", true),
            Member(DataFieldKind::String, "name"),
            Member(DataFieldKind::Float, "speed"),
            Record("nested", { Member(DataFieldKind::Float, "a"), Member(DataFieldKind::Float, "b") }),
            List("mods", Record("", { Member(DataFieldKind::StableKey, "key", true),
                                      Member(DataFieldKind::String, "attr"),
                                      Member(DataFieldKind::Float, "magnitude") })),
            List("tags", Member(DataFieldKind::String, "")),
            facts,
        });
    }

    JsonValue Parse(std::string_view text)
    {
        std::optional<JsonValue> parsed = JsonParse(text);
        EXPECT_TRUE(parsed.has_value()) << text;
        return parsed.value_or(JsonValue{});
    }

    struct Composed
    {
        JsonValue Value;
        CompositionReport Report;

        [[nodiscard]] bool Has(std::string_view code) const
        {
            return std::ranges::any_of(Report.Diagnostics, [&](const CompositionDiagnostic& d) { return d.Code == code; });
        }

        [[nodiscard]] const CompositionDiagnostic* Diagnostic(std::string_view code) const
        {
            const auto found = std::ranges::find(Report.Diagnostics, code, &CompositionDiagnostic::Code);
            return found == Report.Diagnostics.end() ? nullptr : &*found;
        }

        [[nodiscard]] std::vector<std::string> Keys(std::string_view list) const
        {
            std::vector<std::string> keys;
            const JsonValue* array = Value.Find(list);
            if (array != nullptr && array->IsArray())
                for (const JsonValue& element : array->AsArray())
                    if (const JsonValue* key = element.Find(list == "facts" ? "name" : "key"); key != nullptr)
                        keys.push_back(key->AsString());
            return keys;
        }

        [[nodiscard]] const JsonValue* Mod(std::string_view key) const
        {
            for (const JsonValue& element : Value.Find("mods")->AsArray())
                if (element.Find("key")->AsString() == key)
                    return &element;
            return nullptr;
        }
    };

    struct Layers
    {
        std::optional<std::string> Base = std::nullopt;
        const CompositionProvenance* BaseProvenance = nullptr;
        std::vector<std::pair<std::string, std::string>> Includes = {};
        std::string Local = "{}";
        bool Provenance = true;
    };

    Composed Compose(const Layers& layers)
    {
        static const DataFieldSchema schema = DefinitionSchema();
        const CompositionPolicy policy{ .Schema = &schema, .Null = CompositionNull::Removes,
                                        .Provenance = layers.Provenance };
        std::vector<JsonValue> values;
        values.reserve(layers.Includes.size() + 2);
        std::optional<CompositionLayerInput<JsonValue>> base;
        if (layers.Base)
        {
            values.push_back(Parse(*layers.Base));
            base = CompositionLayerInput<JsonValue>{ &values.back(), "base", layers.BaseProvenance };
        }
        std::vector<CompositionLayerInput<JsonValue>> includes;
        for (const auto& [asset, text] : layers.Includes)
        {
            values.push_back(Parse(text));
            includes.push_back({ &values.back(), asset, nullptr });
        }
        values.push_back(Parse(layers.Local));
        const CompositionLayerInput<JsonValue> local{ &values.back(), "child", nullptr };
        Composed composed;
        composed.Value = ComposeLayers<JsonValue>(base ? &*base : nullptr, includes, local, policy, composed.Report);
        return composed;
    }

    constexpr std::string_view kBase = R"({ "req": 1, "name": "a", "speed": 2, "nested": { "a": 1, "b": 2 },
        "mods": [ { "key": "m1", "attr": "A", "magnitude": 1 }, { "key": "m2", "attr": "B", "magnitude": 2 } ],
        "tags": [ "t1", "t2" ], "facts": [ { "name": "Grounded", "kind": "bool" } ] })";
}

TEST(Composition, AnAbsentMemberIsInheritedAndAPresentOneOverrides)
{
    const Composed composed = Compose({ .Base = std::string(kBase), .Local = R"({ "speed": 3, "nested": { "b": 5 } })" });
    EXPECT_TRUE(composed.Report.Diagnostics.empty());
    EXPECT_EQ(composed.Value.Find("name")->AsString(), "a");
    EXPECT_EQ(composed.Value.Find("speed")->AsNumber(), 3.0);
    EXPECT_EQ(composed.Value.Find("nested")->Find("a")->AsNumber(), 1.0) << "a sibling of an edited field stays inherited";
    EXPECT_EQ(composed.Value.Find("nested")->Find("b")->AsNumber(), 5.0);
}

TEST(Composition, NullRemovesAMemberButNeverARequiredOne)
{
    const Composed removed = Compose({ .Base = std::string(kBase), .Local = R"({ "speed": null })" });
    EXPECT_EQ(removed.Value.Find("speed"), nullptr);
    EXPECT_EQ(removed.Report.Removed, std::vector<std::string>{ "$.speed" });

    const Composed refused = Compose({ .Base = std::string(kBase), .Local = R"({ "req": null })" });
    ASSERT_NE(refused.Diagnostic("cannot_clear_required"), nullptr);
    EXPECT_EQ(refused.Diagnostic("cannot_clear_required")->Path, "$.req");
    EXPECT_EQ(refused.Diagnostic("cannot_clear_required")->Asset, "child");
}

TEST(Composition, AKeyedListMergesByKeyAndKeepsInheritedOrder)
{
    const Composed composed = Compose({ .Base = std::string(kBase), .Local = R"({ "mods": {
        "m1": { "magnitude": 5 }, "m3": { "attr": "C", "magnitude": 7 } } })" });
    EXPECT_TRUE(composed.Report.Diagnostics.empty());
    EXPECT_EQ(composed.Keys("mods"), (std::vector<std::string>{ "m1", "m2", "m3" }));
    EXPECT_EQ(composed.Mod("m1")->Find("magnitude")->AsNumber(), 5.0);
    EXPECT_EQ(composed.Mod("m1")->Find("attr")->AsString(), "A") << "only the edited field is overridden";
    EXPECT_EQ(composed.Mod("m3")->Find("key")->AsString(), "m3") << "an added element takes its key from the patch";
}

TEST(Composition, NullRemovesAKeyedElementAndAnUnknownKeyIsAnError)
{
    const Composed composed = Compose({ .Base = std::string(kBase), .Local = R"({ "mods": { "m2": null, "mx": null } })" });
    EXPECT_EQ(composed.Keys("mods"), std::vector<std::string>{ "m1" });
    EXPECT_EQ(composed.Report.Removed, std::vector<std::string>{ "$.mods{m2}" });
    ASSERT_NE(composed.Diagnostic("remove_unknown_key"), nullptr);
    EXPECT_EQ(composed.Diagnostic("remove_unknown_key")->Path, "$.mods{mx}");
}

TEST(Composition, TheListFormReplacesAKeyedListWhole)
{
    const Composed composed = Compose({ .Base = std::string(kBase), .Local = R"({ "mods": [
        { "key": "m2", "attr": "B", "magnitude": 9 }, { "key": "m1", "attr": "A", "magnitude": 1 } ] })" });
    EXPECT_TRUE(composed.Report.Diagnostics.empty());
    EXPECT_EQ(composed.Keys("mods"), (std::vector<std::string>{ "m2", "m1" })) << "the list form is how a child reorders";
}

TEST(Composition, AnElementPatchsKeyMustMatchTheKeyItIsListedUnder)
{
    EXPECT_TRUE(Compose({ .Base = std::string(kBase),
                          .Local = R"({ "mods": { "m1": { "key": "m1", "magnitude": 3 } } })" }).Report.Diagnostics.empty());

    const Composed mismatched = Compose({ .Base = std::string(kBase),
                                          .Local = R"({ "mods": { "m1": { "key": "m9", "magnitude": 3 } } })" });
    ASSERT_NE(mismatched.Diagnostic("key_mismatch"), nullptr);
    EXPECT_EQ(mismatched.Diagnostic("key_mismatch")->Path, "$.mods{m1}.key");
    EXPECT_EQ(mismatched.Mod("m1")->Find("magnitude")->AsNumber(), 1.0) << "a refused patch changes nothing";

    EXPECT_TRUE(Compose({ .Base = std::string(kBase), .Local = R"({ "mods": { "bad key": { "magnitude": 3 } } })" })
                    .Has("invalid_key"));
    EXPECT_TRUE(Compose({ .Base = std::string(kBase), .Local = R"({ "mods": [ { "magnitude": 3 } ] })" })
                    .Has("missing_key"));
}

TEST(Composition, AnUnkeyedListIsReplacedAndRefusesAKeyedPatch)
{
    const Composed replaced = Compose({ .Base = std::string(kBase), .Local = R"({ "tags": [ "t9" ] })" });
    ASSERT_EQ(replaced.Value.Find("tags")->Size(), 1u);
    EXPECT_EQ(replaced.Value.Find("tags")->AsArray()[0].AsString(), "t9");

    const Composed refused = Compose({ .Base = std::string(kBase), .Local = R"({ "tags": { "t1": "x" } })" });
    ASSERT_NE(refused.Diagnostic("keyed_patch_on_unkeyed_array"), nullptr);
    EXPECT_EQ(refused.Diagnostic("keyed_patch_on_unkeyed_array")->Path, "$.tags");
}

TEST(Composition, AnAppendOnlyListTakesNewEntriesOnly)
{
    const Composed appended = Compose({ .Base = std::string(kBase), .Local = R"({ "facts": [ { "name": "Speed", "kind": "float" } ] })" });
    EXPECT_TRUE(appended.Report.Diagnostics.empty());
    EXPECT_EQ(appended.Keys("facts"), (std::vector<std::string>{ "Grounded", "Speed" }))
        << "the list form appends rather than replaces";

    const Composed redeclared = Compose({ .Base = std::string(kBase), .Local = R"({ "facts": [ { "name": "Grounded", "kind": "float" } ] })" });
    ASSERT_NE(redeclared.Diagnostic("redeclares_inherited"), nullptr);
    EXPECT_EQ(redeclared.Diagnostic("redeclares_inherited")->Path, "$.facts{Grounded}");

    EXPECT_TRUE(Compose({ .Base = std::string(kBase), .Local = R"({ "facts": { "Grounded": null } })" }).Has("removes_inherited"));
}

TEST(Composition, FragmentsThatAgreeComposeAndFragmentsThatDisagreeAreAnErrorUntilTheChildChooses)
{
    const Composed agree = Compose({ .Base = std::string(kBase),
                                     .Includes = { { "fire", R"({ "speed": 4, "mods": [ { "key": "m9", "attr": "F", "magnitude": 1 } ] })" },
                                                   { "frost", R"({ "speed": 4 })" } } });
    EXPECT_TRUE(agree.Report.Diagnostics.empty());
    EXPECT_EQ(agree.Value.Find("speed")->AsNumber(), 4.0);
    EXPECT_EQ(agree.Keys("mods"), (std::vector<std::string>{ "m1", "m2", "m9" }))
        << "a fragment adds to a keyed list; it does not replace it";

    const Composed disagree = Compose({ .Base = std::string(kBase),
                                        .Includes = { { "fire", R"({ "speed": 4 })" }, { "frost", R"({ "speed": 6 })" } } });
    const CompositionDiagnostic* conflict = disagree.Diagnostic("include_conflict");
    ASSERT_NE(conflict, nullptr);
    EXPECT_EQ(conflict->Path, "$.speed");
    EXPECT_EQ(conflict->Asset, "child");
    EXPECT_NE(conflict->Message.find("fire"), std::string::npos);
    EXPECT_NE(conflict->Message.find("frost"), std::string::npos);

    EXPECT_TRUE(Compose({ .Base = std::string(kBase),
                          .Includes = { { "fire", R"({ "speed": 4 })" }, { "frost", R"({ "speed": 6 })" } },
                          .Local = R"({ "speed": 5 })" }).Report.Diagnostics.empty())
        << "a local value is the explicit choice";
}

TEST(Composition, FragmentsConflictWhereAWholeRecordAndOneOfItsMembersDisagree)
{
    const Composed agree = Compose({ .Base = std::string(kBase),
                                     .Includes = { { "fire", R"({ "aura": { "radius": 5, "hue": 1 } })" },
                                                   { "frost", R"({ "aura": { "radius": 5 } })" } } });
    EXPECT_TRUE(agree.Report.Diagnostics.empty());

    const Composed disagree = Compose({ .Base = std::string(kBase),
                                        .Includes = { { "fire", R"({ "aura": { "radius": 5, "hue": 1 } })" },
                                                      { "frost", R"({ "aura": { "radius": 9 } })" } } });
    const CompositionDiagnostic* conflict = disagree.Diagnostic("include_conflict");
    ASSERT_NE(conflict, nullptr);
    EXPECT_EQ(conflict->Path, "$.aura.radius");

    const Composed reversed = Compose({ .Base = std::string(kBase),
                                        .Includes = { { "frost", R"({ "aura": { "radius": 9 } })" },
                                                      { "fire", R"({ "aura": { "radius": 5, "hue": 1 } })" } } });
    ASSERT_NE(reversed.Diagnostic("include_conflict"), nullptr) << "in either order";
}

TEST(Composition, ProvenanceNamesTheAssetThatAuthoredEachValue)
{
    const Composed grand = Compose({ .Local = std::string(kBase) });
    const Composed composed = Compose({ .Base = std::string(kBase), .BaseProvenance = &grand.Report.Provenance,
                                        .Includes = { { "fire", R"({ "speed": 4 })" } },
                                        .Local = R"({ "mods": { "m1": { "magnitude": 5 } } })" });
    const CompositionProvenance& provenance = composed.Report.Provenance;

    const CompositionOrigin* name = FindCompositionOrigin(provenance, "$.name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->Layer, CompositionLayer::Base);
    EXPECT_EQ(name->Asset, "child") << "the grandparent's own local layer authored it, under the asset name it was given";

    const CompositionOrigin* speed = FindCompositionOrigin(provenance, "$.speed");
    ASSERT_NE(speed, nullptr);
    EXPECT_EQ(speed->Layer, CompositionLayer::Include);
    EXPECT_EQ(speed->Asset, "fire");

    const CompositionOrigin* magnitude = FindCompositionOrigin(provenance, "$.mods{m1}.magnitude");
    ASSERT_NE(magnitude, nullptr);
    EXPECT_EQ(magnitude->Layer, CompositionLayer::Local);
    const CompositionOrigin* attr = FindCompositionOrigin(provenance, "$.mods{m1}.attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_EQ(attr->Layer, CompositionLayer::Base) << "the element's other fields stay inherited";

    const Composed removed = Compose({ .Base = std::string(kBase), .Local = R"({ "mods": { "m2": null } })" });
    EXPECT_EQ(FindCompositionOrigin(removed.Report.Provenance, "$.mods{m2}.attr"), nullptr);
}

TEST(Composition, AnEmptyRecordHasAnOriginOfItsOwn)
{
    const Composed composed = Compose({ .Base = R"({ "req": 1, "nested": {} })", .Local = "{}" });
    const auto found = composed.Report.Provenance.find("$.nested");
    ASSERT_NE(found, composed.Report.Provenance.end()) << "a record with no members is still a value someone set";
    EXPECT_EQ(found->second.Layer, CompositionLayer::Base);
}

TEST(Composition, ACompositionsIndexPathsMapToKeys)
{
    const Composed composed = Compose({ .Base = std::string(kBase), .Local = R"({ "mods": { "m1": null } })" });
    static const DataFieldSchema schema = DefinitionSchema();
    EXPECT_EQ(CompositionLogicalPath(composed.Value, schema, "$.mods[0].magnitude"), "$.mods{m2}.magnitude");
    EXPECT_EQ(CompositionLogicalPath(composed.Value, schema, "$.tags[1]"), "$.tags[1]");
    EXPECT_EQ(CompositionLogicalPath(composed.Value, schema, "$.facts[0].name"), "$.facts{Grounded}.name");
}

namespace
{
    Composed RoundTrip(const DataFieldSchema& schema, const JsonValue& base, const JsonValue& target)
    {
        const std::optional<JsonValue> patch = PatchFrom(base, target, &schema);
        const JsonValue nothing{ JsonValue::Object{} };
        const CompositionLayerInput<JsonValue> baseLayer{ &base, "base", nullptr };
        const CompositionLayerInput<JsonValue> local{ patch ? &*patch : &nothing, "save", nullptr };
        Composed composed;
        composed.Value = ComposeLayers<JsonValue>(&baseLayer, {}, local,
                                                  { .Schema = &schema, .Null = CompositionNull::Removes },
                                                  composed.Report);
        return composed;
    }

    std::string Codes(const CompositionReport& report)
    {
        std::string codes;
        for (const CompositionDiagnostic& diagnostic : report.Diagnostics)
            codes += diagnostic.Code + " at " + diagnostic.Path + "; ";
        return codes;
    }
}

TEST(CompositionPatch, AKeyedListsNewOrderComposesBack)
{
    const DataFieldSchema schema = CompositionRoundTrip::Schema();
    const JsonValue base = Parse(R"({ "req": 1, "mods": [ { "key": "a", "magnitude": 1 }, { "key": "b", "magnitude": 2 } ] })");
    for (const std::string_view target :
         { R"({ "req": 1, "mods": [ { "key": "b", "magnitude": 2 }, { "key": "a", "magnitude": 1 } ] })",
           R"({ "req": 1, "mods": [ { "key": "a", "magnitude": 1 }, { "key": "new", "magnitude": 3 },
                                    { "key": "b", "magnitude": 2 } ] })" })
    {
        const JsonValue live = Parse(target);
        const Composed composed = RoundTrip(schema, base, live);
        EXPECT_TRUE(composed.Report.Diagnostics.empty()) << Codes(composed.Report);
        EXPECT_TRUE(JsonEquals(composed.Value, live)) << target << " came back as " << JsonStringify(composed.Value);
    }
}

TEST(CompositionPatch, AKeyedListWithARepeatedOrUnspellableKeyComposesBack)
{
    const DataFieldSchema schema = CompositionRoundTrip::Schema();
    const JsonValue base = Parse(R"({ "req": 1, "mods": [ { "key": "a", "magnitude": 1 } ] })");
    for (const std::string_view target :
         { R"({ "req": 1, "mods": [ { "key": "a", "magnitude": 1 }, { "key": "a", "magnitude": 5 } ] })",
           R"({ "req": 1, "mods": [ { "key": "a", "magnitude": 1 }, { "key": "not a key", "magnitude": 5 } ] })" })
    {
        const JsonValue live = Parse(target);
        const Composed composed = RoundTrip(schema, base, live);
        EXPECT_TRUE(composed.Report.Diagnostics.empty()) << Codes(composed.Report);
        EXPECT_TRUE(JsonEquals(composed.Value, live)) << target << " came back as " << JsonStringify(composed.Value);
    }
}

TEST(CompositionPatch, WhatNoPatchCanStateIsReportedWhenComposed)
{
    const DataFieldSchema schema = CompositionRoundTrip::Schema();
    const JsonValue base = Parse(R"({ "req": 1, "mods": [ { "key": "a" } ],
        "facts": [ { "name": "Grounded", "kind": "bool" }, { "name": "Speed", "kind": "float" } ] })");
    EXPECT_TRUE(RoundTrip(schema, base, Parse(R"({ "req": 1, "mods": [ { "key": "a" }, { "magnitude": 2 } ],
        "facts": [ { "name": "Grounded", "kind": "bool" }, { "name": "Speed", "kind": "float" } ] })")).Has("missing_key"));
    EXPECT_TRUE(RoundTrip(schema, base, Parse(R"({ "req": 1, "mods": [ { "key": "a" } ],
        "facts": [ { "name": "Grounded", "kind": "int" }, { "name": "Speed", "kind": "float" } ] })")).Has("redeclares_inherited"));
    EXPECT_TRUE(RoundTrip(schema, base, Parse(R"({ "req": 1, "mods": [ { "key": "a" } ],
        "facts": [ { "name": "Speed", "kind": "float" } ] })")).Has("removes_inherited"));
    EXPECT_TRUE(RoundTrip(schema, base, Parse(R"({ "req": 1, "mods": [ { "key": "a" } ],
        "facts": [ { "name": "Speed", "kind": "float" }, { "name": "Grounded", "kind": "bool" } ] })")).Has("redeclares_inherited"))
        << "an append-only list's inherited entries keep their order";
    EXPECT_TRUE(RoundTrip(schema, base, Parse(R"({ "mods": [ { "key": "a" } ],
        "facts": [ { "name": "Grounded", "kind": "bool" }, { "name": "Speed", "kind": "float" } ] })")).Has("cannot_clear_required"));
}

TEST(CompositionPatch, EveryTargetComposesBackFromItsPatch)
{
    const DataFieldSchema schema = CompositionRoundTrip::Schema();
    CompositionRoundTrip::Generator generator(20261119u);
    for (int i = 0; i < 2000; ++i)
    {
        const CompositionRoundTrip::Case pair = generator.Next();
        const JsonValue base = Parse(pair.Base);
        const JsonValue target = Parse(pair.Target);
        const Composed composed = RoundTrip(schema, base, target);
        ASSERT_TRUE(composed.Report.Diagnostics.empty())
            << Codes(composed.Report) << "\nbase   " << pair.Base << "\ntarget " << pair.Target;
        ASSERT_TRUE(JsonEquals(composed.Value, target))
            << "base   " << pair.Base << "\ntarget " << pair.Target << "\ngot    " << JsonStringify(composed.Value);
    }
}

TEST(Composition, WithoutASchemaObjectsMergeAndAnythingElseIsWrittenWhole)
{
    JsonValue target = Parse(R"({ "a": { "x": 1 }, "list": [ 1, 2 ], "keep": 3 })");
    const JsonValue patch = Parse(R"({ "a": { "y": 2 }, "list": [ 9 ], "gone": null })");
    CompositionReport report;
    ApplyCompositionPatch(target, patch, "scene", CompositionPolicy{ .Schema = nullptr, .Null = CompositionNull::IsAValue },
                          report);
    EXPECT_TRUE(report.Diagnostics.empty());
    EXPECT_EQ(target.Find("a")->Find("x")->AsNumber(), 1.0);
    EXPECT_EQ(target.Find("a")->Find("y")->AsNumber(), 2.0);
    ASSERT_EQ(target.Find("list")->Size(), 1u);
    ASSERT_NE(target.Find("gone"), nullptr) << "with null as a value, null is written";
    EXPECT_TRUE(target.Find("gone")->IsNull());
    EXPECT_EQ(target.Find("keep")->AsNumber(), 3.0);
}
