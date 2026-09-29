// Names content uses that nothing declares, found through the fields the
// rig's diagnostics point at -- only fields a schema types as gameplay tags.

#include "authoring/AnimationNameDeclarations.h"

#include <anim/AnimRigData.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/json/JsonParser.h>
#include <core/metadata/DataSchema.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <gtest/gtest.h>

namespace
{
    constexpr std::string_view kRig = R"({ "type": "animation.rig", "version": 1, "data": {
        "facts": "asset://engine.facts.sdata",
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" },
                    { "name": "anim.layer.upper", "idle": "Anim.Upper.Rest" } ] } })";

    struct Schemas
    {
        DataAssetTypeRegistry Types;
        DataSchemaRegistry Registry;
        Schemas() { RegisterAnimRigData(Types, Registry); }
        const DataFieldSchema& Rig() const { return Registry.Find(kAnimRigType)->Root; }
    };

    AnimDiagnostic At(std::string field)
    {
        return { .Severity = AnimDiagnosticSeverity::Error, .Code = "test", .AssetPath = "asset://rig.sdata",
                 .FieldPath = std::move(field), .Message = {} };
    }
}

TEST(AnimationNameDeclarations, APathWalksTheDocumentAndItsSchemaTogether)
{
    Schemas schemas;
    const JsonValue root = *JsonParse(kRig);
    const AnimationFieldAt idle = FindAnimationField(root, schemas.Rig(), "$.data.layers[1].idle");
    ASSERT_NE(idle.Value, nullptr);
    EXPECT_EQ(idle.Value->AsString(), "Anim.Upper.Rest");
    EXPECT_EQ(idle.Field->Kind, DataFieldKind::GameplayTag);
    EXPECT_EQ(FindAnimationField(root, schemas.Rig(), "$.data.facts").Field->Kind, DataFieldKind::DataAssetRef);

    for (const char* nowhere : { "$.data.layers[2].idle", "$.data.layers[x].idle", "$.data.nope", "$.data.layers[0].",
                                 "data.layers", "$.data.layers[0" })
        EXPECT_EQ(FindAnimationField(root, schemas.Rig(), nowhere).Value, nullptr) << nowhere;
}

// A misspelt reference to another asset is a problem, but not a name to
// declare: only tag-typed fields are offered, once each, and only unknown ones.
TEST(AnimationNameDeclarations, OnlyUnknownTagFieldsAreOffered)
{
    Schemas schemas;
    const JsonValue root = *JsonParse(kRig);
    GameplayTagRegistry tags;
    (void)tags.RegisterTag("Anim.Idle");
    const std::vector<std::string> names = UndeclaredAnimationNames(
        { At("$.data.layers[0].idle"), At("$.data.layers[1].idle"), At("$.data.facts"), At("$.data.layers[1].idle"),
          At("$.data.layers[1].name") },
        tags, [&](std::string_view) { return AnimationDocumentView{ &root, &schemas.Rig() }; });
    EXPECT_EQ(names, (std::vector<std::string>{ "Anim.Upper.Rest", "anim.layer.upper" }));

    EXPECT_TRUE(UndeclaredAnimationNames({ At("$.data.layers[1].idle") }, tags,
                                         [](std::string_view) { return AnimationDocumentView{}; })
                    .empty())
        << "a document that cannot be read offers nothing";
}

TEST(AnimationNameDeclarations, DeclaringAddsOnlyWhatIsNotListed)
{
    JsonValue root = *JsonParse(R"({ "type": "gameplay.tag_declarations", "version": 1,
                                     "data": { "tags": [ "Anim.Idle" ] } })");
    EXPECT_TRUE(AddAnimationTagDeclarations(root, { "Anim.Idle", "Anim.Walk" }));
    EXPECT_FALSE(AddAnimationTagDeclarations(root, { "Anim.Walk" }));
    const JsonValue::Array& tags = root.Find("data")->Find("tags")->AsArray();
    ASSERT_EQ(tags.size(), 2u);
    EXPECT_EQ(tags[1].AsString(), "Anim.Walk");

    JsonValue empty = *JsonParse(R"({ "type": "gameplay.tag_declarations", "version": 1, "data": {} })");
    EXPECT_TRUE(AddAnimationTagDeclarations(empty, { "Anim.Idle" }));
    EXPECT_EQ(empty.Find("data")->Find("tags")->AsArray().size(), 1u);
}
