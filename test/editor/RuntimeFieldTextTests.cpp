#include <gtest/gtest.h>

#include "ui/RuntimeFieldText.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <string>

// A component field as the text somebody reads and types, and back.
//
// This is where an authored inspector is most likely to be quietly wrong: a
// vector whose third number is nonsense half-applied, a field shown in degrees
// written back in degrees, an enum written to a value no schema names. None of
// those need a document or a window to catch, so none of them are tested
// through one.

namespace
{
struct Sample
{
    float         Position[3];
    bool          Flag;
    std::int32_t  Count;
    double        Wide;
    float         Angle;
    std::uint64_t Identity;
    std::uint8_t  Mode;
};

enum class SampleMode : std::uint8_t { Off = 0, Slow = 1, Fast = 2 };

constexpr EnumOption kModes[] = {
    EnumOption{ 0, "off" },
    EnumOption{ 1, "slow" },
    EnumOption{ 2, "fast" },
};

[[nodiscard]] RuntimeField Vector3Field()
{
    RuntimeField field;
    field.Name = "local.position";
    field.Offset = offsetof(Sample, Position);
    field.Size = sizeof(float);
    field.Scalar = FieldScalar::Float;
    field.Count = 3;
    return field;
}

[[nodiscard]] RuntimeField BoolField()
{
    RuntimeField field;
    field.Name = "play_on_active";
    field.Offset = offsetof(Sample, Flag);
    field.Size = sizeof(bool);
    field.Scalar = FieldScalar::Bool;
    return field;
}

[[nodiscard]] RuntimeField AngleField()
{
    RuntimeField field;
    field.Name = "angle";
    field.Offset = offsetof(Sample, Angle);
    field.Size = sizeof(float);
    field.Scalar = FieldScalar::Float;
    field.DisplayDegrees = true;
    return field;
}

[[nodiscard]] RuntimeField ModeField()
{
    RuntimeField field;
    field.Name = "mode";
    field.Offset = offsetof(Sample, Mode);
    field.Size = sizeof(std::uint8_t);
    field.Scalar = FieldScalar::UInt32;
    field.Enum = kModes;
    return field;
}

[[nodiscard]] RuntimeField IdentityField()
{
    RuntimeField field;
    field.Name = "id";
    field.Offset = offsetof(Sample, Identity);
    field.Size = sizeof(std::uint64_t);
    field.Scalar = FieldScalar::UInt64;
    field.ReadOnly = true;
    return field;
}
} // namespace

TEST(RuntimeFieldText, AVectorReadsAndWritesAsOneCommaSeparatedRow)
{
    Sample sample{};
    sample.Position[0] = 1.5f;
    sample.Position[1] = -2.0f;
    sample.Position[2] = 0.0f;

    const RuntimeField field = Vector3Field();
    EXPECT_EQ(FormatRuntimeField(field, &sample), "1.5, -2, 0");

    ASSERT_TRUE(ParseRuntimeField(field, " 3 , 4.25,-5 ", &sample));
    EXPECT_FLOAT_EQ(sample.Position[0], 3.0f);
    EXPECT_FLOAT_EQ(sample.Position[1], 4.25f);
    EXPECT_FLOAT_EQ(sample.Position[2], -5.0f);
}

TEST(RuntimeFieldText, ABadComponentLeavesTheWholeVectorAlone)
{
    // The all-or-nothing rule. Writing the first two numbers and refusing the
    // third would move a brush somewhere nobody asked for.
    Sample sample{};
    sample.Position[0] = 1.0f;
    sample.Position[1] = 2.0f;
    sample.Position[2] = 3.0f;

    const RuntimeField field = Vector3Field();
    EXPECT_FALSE(ParseRuntimeField(field, "9, 9, banana", &sample));
    EXPECT_FLOAT_EQ(sample.Position[0], 1.0f);
    EXPECT_FLOAT_EQ(sample.Position[1], 2.0f);
    EXPECT_FLOAT_EQ(sample.Position[2], 3.0f);
}

TEST(RuntimeFieldText, TheNumberOfValuesHasToMatchTheField)
{
    Sample sample{};
    const RuntimeField field = Vector3Field();
    EXPECT_FALSE(ParseRuntimeField(field, "1, 2", &sample));
    EXPECT_FALSE(ParseRuntimeField(field, "1, 2, 3, 4", &sample));
    EXPECT_FLOAT_EQ(sample.Position[0], 0.0f);

    // And a scalar field takes exactly one, rather than silently keeping the
    // first of several.
    RuntimeField scalar = Vector3Field();
    scalar.Count = 1;
    EXPECT_FALSE(ParseRuntimeField(scalar, "1, 2", &sample));
    EXPECT_FLOAT_EQ(sample.Position[0], 0.0f);
}

TEST(RuntimeFieldText, DegreesAreShownAndTakenInDegreesAndStoredInRadians)
{
    Sample sample{};
    sample.Angle = static_cast<float>(std::numbers::pi) * 0.5f;

    const RuntimeField field = AngleField();
    EXPECT_EQ(FormatRuntimeField(field, &sample), "90");

    ASSERT_TRUE(ParseRuntimeField(field, "180", &sample));
    EXPECT_NEAR(sample.Angle, std::numbers::pi_v<float>, 1e-5f);
}

TEST(RuntimeFieldText, ABoolTakesTheWordsAndTheDigitsAndNothingElse)
{
    Sample sample{};
    const RuntimeField field = BoolField();
    EXPECT_EQ(FormatRuntimeField(field, &sample), "false");

    ASSERT_TRUE(ParseRuntimeField(field, "True", &sample));
    EXPECT_TRUE(sample.Flag);
    EXPECT_EQ(FormatRuntimeField(field, &sample), "true");

    ASSERT_TRUE(ParseRuntimeField(field, "0", &sample));
    EXPECT_FALSE(sample.Flag);

    EXPECT_FALSE(ParseRuntimeField(field, "yes", &sample));
    EXPECT_FALSE(sample.Flag);
}

TEST(RuntimeFieldText, AnEnumReadsItsNameAndRefusesAValueNoSchemaNames)
{
    Sample sample{};
    sample.Mode = static_cast<std::uint8_t>(SampleMode::Slow);

    const RuntimeField field = ModeField();
    EXPECT_EQ(FormatRuntimeField(field, &sample), "slow");

    ASSERT_TRUE(ParseRuntimeField(field, "FAST", &sample));
    EXPECT_EQ(sample.Mode, 2u);

    // A number is a legitimate way to name an enumerator; a number the enum
    // does not declare is a component nothing can round-trip.
    ASSERT_TRUE(ParseRuntimeField(field, "0", &sample));
    EXPECT_EQ(sample.Mode, 0u);
    EXPECT_FALSE(ParseRuntimeField(field, "7", &sample));
    EXPECT_EQ(sample.Mode, 0u);
}

TEST(RuntimeFieldText, AnIdentityIsShownAndNotOffered)
{
    Sample sample{};
    sample.Identity = 18446744073709551615ull;

    const RuntimeField field = IdentityField();
    EXPECT_FALSE(IsRuntimeFieldEditable(field));
    EXPECT_EQ(FormatRuntimeField(field, &sample), "18446744073709551615");
    EXPECT_FALSE(ParseRuntimeField(field, "1", &sample));
    EXPECT_EQ(sample.Identity, 18446744073709551615ull);
}

TEST(RuntimeFieldText, AnAssetHandleIsNeverOfferedAsText)
{
    // Asset handles are refcounted and session-local, so they travel through
    // their own command. A blind byte write here would corrupt a refcount.
    RuntimeField field = Vector3Field();
    field.Asset = AssetType::Texture;
    EXPECT_FALSE(IsRuntimeFieldEditable(field));

    Sample sample{};
    EXPECT_FALSE(ParseRuntimeField(field, "1, 2, 3", &sample));
    EXPECT_FLOAT_EQ(sample.Position[0], 0.0f);
}

TEST(RuntimeFieldText, WhatIsShownParsesBackToTheSameBytes)
{
    // The round trip the surface relies on to tell an edit from a click: a
    // field somebody focused and left alone must not become a change.
    Sample sample{};
    sample.Position[0] = 0.125f;
    sample.Position[1] = -1024.5f;
    sample.Position[2] = 3.0f;

    const RuntimeField field = Vector3Field();
    const std::string shown = FormatRuntimeField(field, &sample);

    Sample copy = sample;
    ASSERT_TRUE(ParseRuntimeField(field, shown, &copy));
    EXPECT_EQ(std::memcmp(&copy, &sample, sizeof(Sample)), 0);
}

TEST(RuntimeFieldText, ALabelComesFromTheFieldOrItsName)
{
    RuntimeField field = Vector3Field();
    EXPECT_EQ(RuntimeFieldLabel(field), "Position");

    field.Name = "play_on_active";
    EXPECT_EQ(RuntimeFieldLabel(field), "Play On Active");

    field.Label = "Start Playing";
    EXPECT_EQ(RuntimeFieldLabel(field), "Start Playing");
}
