#include "brush/BrushMeshSerialization.h"
#include "brush/BrushModifierSerialization.h"
#include "brush/BrushOps.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>

#include <gtest/gtest.h>

namespace
{
    BrushRecord Sample()
    {
        BrushRecord record;
        record.Mesh = BrushOps::MakeBox({ 1, 1, 1 });
        BrushModifier mirror;
        MirrorModifier custom;
        custom.Source = MirrorPlaneSource::Custom;
        custom.CustomPlane = Plane::FromNormalAndDistance({ 0, 0, 1 }, -1.5f);
        mirror.Params = custom;
        mirror.Enabled = false;
        BrushModifier array;
        ArrayModifier constant;
        constant.Placement = ArrayPlacement::ConstantOffset;
        constant.Count = 4;
        constant.Offset = { 2.5f, 0, -1 };
        array.Params = constant;
        record.Modifiers = { mirror, array };
        return record;
    }

    JsonValue Reparse(const JsonValue& value)
    {
        const std::optional<JsonValue> parsed = JsonParse(JsonStringify(value, /*pretty*/ false));
        EXPECT_TRUE(parsed.has_value());
        return parsed.value_or(JsonValue{});
    }
}

TEST(BrushModifierSerialization, RecordRoundTripsKindsOrderAndFlags)
{
    const BrushRecord record = Sample();
    std::string error;
    const BrushRecord back = BrushRecordFromJson(Reparse(BrushRecordToJson(record)), &error);
    EXPECT_TRUE(error.empty()) << error;
    ASSERT_EQ(back.Modifiers.size(), 2u);
    EXPECT_FALSE(back.Modifiers[0].Enabled);
    EXPECT_TRUE(back.Modifiers[1].Enabled);
    const auto* mirror = std::get_if<MirrorModifier>(&back.Modifiers[0].Params);
    ASSERT_NE(mirror, nullptr);
    EXPECT_EQ(mirror->Source, MirrorPlaneSource::Custom);
    EXPECT_FLOAT_EQ(mirror->CustomPlane.Normal.Z, 1.0f);
    EXPECT_FLOAT_EQ(mirror->CustomPlane.D, -1.5f);
    const auto* array = std::get_if<ArrayModifier>(&back.Modifiers[1].Params);
    ASSERT_NE(array, nullptr);
    EXPECT_EQ(array->Placement, ArrayPlacement::ConstantOffset);
    EXPECT_EQ(array->Count, 4);
    EXPECT_FLOAT_EQ(array->Offset.X, 2.5f);
    EXPECT_FLOAT_EQ(array->Offset.Z, -1.0f);
    EXPECT_EQ(back.Mesh.Faces.size(), record.Mesh.Faces.size());
}

TEST(BrushModifierSerialization, MissingModifiersKeyLoadsAnEmptyStack)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushRecord back = BrushRecordFromJson(Reparse(BrushMeshToJson(box)));
    EXPECT_TRUE(back.Modifiers.empty());
    EXPECT_EQ(back.Mesh.Vertices.size(), 8u);
}

TEST(BrushModifierSerialization, RecordWithoutModifiersSerializesExactlyLikeTheMesh)
{
    BrushRecord record;
    record.Mesh = BrushOps::MakeBox({ 1, 2, 3 });
    EXPECT_EQ(JsonStringify(BrushRecordToJson(record), false), JsonStringify(BrushMeshToJson(record.Mesh), false));
}

TEST(BrushModifierSerialization, MalformedEntriesAreSkippedAndNamed)
{
    const char* text = R"({
        "vertices": [[-1,-1,-1],[1,-1,-1],[1,1,-1],[-1,1,-1]],
        "faces": [[0,1,2,3]],
        "modifiers": [ { "kind": "bevel" }, { "kind": "mirror", "source": "custom" },
                       { "kind": "array", "count": 3, "offset": [1,0,0] } ]
    })";
    const std::optional<JsonValue> parsed = JsonParse(text);
    ASSERT_TRUE(parsed.has_value());
    std::string error;
    const BrushRecord back = BrushRecordFromJson(*parsed, &error);
    ASSERT_EQ(back.Modifiers.size(), 1u);
    EXPECT_NE(error.find("modifier 0: unknown kind"), std::string::npos) << error;
    EXPECT_NE(error.find("modifier 1: malformed parameters"), std::string::npos) << error;
}

TEST(BrushModifierSerialization, StoreRoundTripCarriesEveryRecord)
{
    BrushMeshStore store;
    const BrushId id = store.Create(Sample());
    (void)store.Create(BrushOps::MakeBox({ 2, 2, 2 }));
    BrushMeshStore back;
    std::string error;
    DeserializeBrushMeshes(Reparse(SerializeBrushMeshes(store)), back, &error);
    EXPECT_TRUE(error.empty());
    ASSERT_EQ(back.Count(), 2u);
    ASSERT_NE(back.FindModifiers(id), nullptr);
    EXPECT_EQ(back.FindModifiers(id)->size(), 2u);
}

TEST(BrushModifierSerialization, RelationshipModesRoundTrip)
{
    BrushRecord record;
    record.Mesh = BrushOps::MakeBox({ 1, 1, 1 });
    BrushModifier mirror;
    MirrorModifier m;
    m.Axis = LocalAxis::Z;
    m.Source = MirrorPlaneSource::BoundsCenter;
    m.Offset = 2.5f;
    mirror.Params = m;
    BrushModifier array;
    ArrayModifier a;
    a.Axis = LocalAxis::Y;
    a.Count = 7;
    a.Spacing = 16.0f;
    a.Reverse = true;
    array.Params = a;
    record.Modifiers = { mirror, array };

    const JsonValue json = BrushRecordToJson(record);
    const JsonValue* entries = json.Find("modifiers");
    ASSERT_NE(entries, nullptr);
    ASSERT_EQ(entries->AsArray().size(), 2u);
    EXPECT_EQ(entries->AsArray()[0].Find("plane"), nullptr);  // no stored plane unless Custom
    EXPECT_EQ(entries->AsArray()[1].Find("offset"), nullptr); // no vector unless ConstantOffset

    std::string error;
    const BrushRecord back = BrushRecordFromJson(Reparse(json), &error);
    EXPECT_TRUE(error.empty()) << error;
    ASSERT_EQ(back.Modifiers.size(), 2u);
    const auto& bm = std::get<MirrorModifier>(back.Modifiers[0].Params);
    EXPECT_EQ(bm.Axis, LocalAxis::Z);
    EXPECT_EQ(bm.Source, MirrorPlaneSource::BoundsCenter);
    EXPECT_FLOAT_EQ(bm.Offset, 2.5f);
    const auto& ba = std::get<ArrayModifier>(back.Modifiers[1].Params);
    EXPECT_EQ(ba.Placement, ArrayPlacement::RelativeToBounds);
    EXPECT_EQ(ba.Axis, LocalAxis::Y);
    EXPECT_EQ(ba.Count, 7);
    EXPECT_FLOAT_EQ(ba.Spacing, 16.0f);
    EXPECT_TRUE(ba.Reverse);
}

TEST(BrushModifierSerialization, LegacyEntriesKeepTheirMeaning)
{
    const char* text = R"({
        "vertices": [[-1,-1,-1],[1,-1,-1],[1,1,-1],[-1,1,-1]],
        "faces": [[0,1,2,3]],
        "modifiers": [ { "kind": "mirror", "plane": { "normal": [0,0,1], "d": -1.5 } },
                       { "kind": "array", "count": 3, "offset": [1,0,0] } ]
    })";
    const std::optional<JsonValue> parsed = JsonParse(text);
    ASSERT_TRUE(parsed.has_value());
    std::string error;
    const BrushRecord back = BrushRecordFromJson(*parsed, &error);
    EXPECT_TRUE(error.empty()) << error;
    ASSERT_EQ(back.Modifiers.size(), 2u);
    const auto& m = std::get<MirrorModifier>(back.Modifiers[0].Params);
    EXPECT_EQ(m.Source, MirrorPlaneSource::Custom);
    EXPECT_FLOAT_EQ(m.CustomPlane.D, -1.5f);
    const auto& a = std::get<ArrayModifier>(back.Modifiers[1].Params);
    EXPECT_EQ(a.Placement, ArrayPlacement::ConstantOffset);
    EXPECT_FLOAT_EQ(a.Offset.X, 1.0f);
}
