#include <gtest/gtest.h>

#include "authoring/RuntimeFieldText.h"

#include <core/text/InlineString.h>
#include <math/Quat.h>

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

TEST(RuntimeFieldText, OneNumberOfAVectorIsReadAndWrittenAlone)
{
    Sample sample{};
    sample.Position[0] = 1.0f;
    sample.Position[1] = 2.0f;
    sample.Position[2] = 3.0f;
    const RuntimeField field = Vector3Field();
    EXPECT_EQ(FormatRuntimeFieldElement(field, &sample, 1), "2");
    EXPECT_TRUE(FormatRuntimeFieldElement(field, &sample, 3).empty());

    ASSERT_TRUE(ParseRuntimeFieldElement(field, 1, "7.5", &sample));
    EXPECT_FLOAT_EQ(sample.Position[0], 1.0f);
    EXPECT_FLOAT_EQ(sample.Position[1], 7.5f);
    EXPECT_FLOAT_EQ(sample.Position[2], 3.0f);
    EXPECT_FALSE(ParseRuntimeFieldElement(field, 1, "nonsense", &sample));
    EXPECT_FLOAT_EQ(sample.Position[1], 7.5f) << "a refused number leaves it as it was";

    ASSERT_TRUE(OffsetRuntimeFieldElement(field, 2, -0.5, &sample));
    EXPECT_FLOAT_EQ(sample.Position[2], 2.5f);
}

TEST(RuntimeFieldText, AMovedNumberMovesInTheUnitsItIsShownInAndKeepsItsKind)
{
    Sample sample{};
    sample.Angle = 0.0f;
    ASSERT_TRUE(OffsetRuntimeFieldElement(AngleField(), 0, 90.0, &sample));
    EXPECT_NEAR(sample.Angle, std::numbers::pi_v<float> * 0.5f, 1e-5f) << "ninety degrees, stored in radians";

    RuntimeField count;
    count.Name = "count";
    count.Offset = offsetof(Sample, Count);
    count.Size = sizeof(std::int32_t);
    count.Scalar = FieldScalar::Int32;
    sample.Count = 3;
    ASSERT_TRUE(OffsetRuntimeFieldElement(count, 0, 1.6, &sample));
    EXPECT_EQ(sample.Count, 5) << "a whole number moved stays whole";
    EXPECT_FALSE(OffsetRuntimeFieldElement(BoolField(), 0, 1.0, &sample)) << "a switch has no amount";
}

TEST(RuntimeFieldText, ALoneNumberIsReadInTheUnitsItIsShownIn)
{
    Sample sample{};
    sample.Angle = std::numbers::pi_v<float> * 0.25f;
    sample.Count = -4;
    sample.Wide = 2.5;
    ASSERT_TRUE(RuntimeFieldNumber(AngleField(), &sample).has_value());
    EXPECT_NEAR(*RuntimeFieldNumber(AngleField(), &sample), 45.0, 1e-4);

    RuntimeField count;
    count.Name = "count";
    count.Offset = offsetof(Sample, Count);
    count.Size = sizeof(std::int32_t);
    count.Scalar = FieldScalar::Int32;
    EXPECT_EQ(RuntimeFieldNumber(count, &sample), -4.0);
    RuntimeField wide;
    wide.Name = "wide";
    wide.Offset = offsetof(Sample, Wide);
    wide.Size = sizeof(double);
    wide.Scalar = FieldScalar::Double;
    EXPECT_EQ(RuntimeFieldNumber(wide, &sample), 2.5);

    EXPECT_FALSE(RuntimeFieldNumber(Vector3Field(), &sample).has_value()) << "several numbers are not one";
    EXPECT_FALSE(RuntimeFieldNumber(BoolField(), &sample).has_value());
    EXPECT_FALSE(RuntimeFieldNumber(ModeField(), &sample).has_value()) << "a choice is read by its name";
    EXPECT_FALSE(RuntimeFieldNumber(AngleField(), nullptr).has_value());
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

TEST(RuntimeFieldText, ANameRunTogetherReadsAsItsWords)
{
    EXPECT_EQ(HumanizeSchemaName("CharacterMovement"), "Character Movement");
    EXPECT_EQ(HumanizeSchemaName("sencha.world_transform"), "World Transform");
    EXPECT_EQ(HumanizeSchemaName("Lod2Mesh"), "Lod2 Mesh");
    EXPECT_EQ(HumanizeSchemaName("AABB"), "AABB") << "an acronym stays one word";
}

// InlineString members: the one string form a component may hold, because
// archetype storage relocates with memcpy. They are bytes at an offset like
// everything else, so they read and write here rather than needing a component
// to be named.

namespace
{
struct Named
{
    InlineString<8> Short;
    float           After;
};

[[nodiscard]] RuntimeField TextField()
{
    RuntimeField field;
    field.Name = "value";
    field.Offset = offsetof(Named, Short);
    field.Size = sizeof(InlineString<8>);
    field.Scalar = FieldScalar::Unsupported;
    field.InlineText = true;
    return field;
}
} // namespace

TEST(RuntimeFieldText, InlineTextReadsAndWritesItsCharacters)
{
    Named sample{};
    sample.Short = "crate";
    sample.After = 1.5f;

    const RuntimeField field = TextField();
    EXPECT_TRUE(IsRuntimeFieldEditable(field));
    EXPECT_EQ(FormatRuntimeField(field, &sample), "crate");

    ASSERT_TRUE(ParseRuntimeField(field, "door", &sample));
    EXPECT_EQ(sample.Short.View(), "door");
    EXPECT_FLOAT_EQ(sample.After, 1.5f) << "writing the text ran past the field";
}

TEST(RuntimeFieldText, TextIsNotSplitOnCommas)
{
    // The comma rule belongs to numbers. A name is whatever was typed.
    Named sample{};
    const RuntimeField field = TextField();

    ASSERT_TRUE(ParseRuntimeField(field, "a, b", &sample));
    EXPECT_EQ(sample.Short.View(), "a, b");
}

TEST(RuntimeFieldText, TextTooLongIsRefusedRatherThanTruncated)
{
    // Everywhere else an InlineString assignment truncates, which is right when
    // the caller is code. Here it is a person watching their own characters
    // disappear.
    Named sample{};
    sample.Short = "keep";

    const RuntimeField field = TextField();
    EXPECT_FALSE(ParseRuntimeField(field, "far too long for eight", &sample));
    EXPECT_EQ(sample.Short.View(), "keep");

    // Exactly the capacity minus its terminator still fits.
    ASSERT_TRUE(ParseRuntimeField(field, "seven77", &sample));
    EXPECT_EQ(sample.Short.View(), "seven77");
}

TEST(RuntimeFieldText, AShorterNameZeroesWhatTheLongerOneLeft)
{
    // InlineString compares and hashes the whole buffer, so a leftover tail
    // would make two equal names unequal.
    Named sample{};
    const RuntimeField field = TextField();

    ASSERT_TRUE(ParseRuntimeField(field, "seven77", &sample));
    ASSERT_TRUE(ParseRuntimeField(field, "ab", &sample));
    EXPECT_EQ(sample.Short, InlineString<8>("ab"));
}

// A rotation is a quaternion in its bytes and pitch, yaw and roll in degrees
// to a person; one angle moves alone, from what the others were.
namespace
{
struct Turned
{
    float Rotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
};

[[nodiscard]] RuntimeField RotationField()
{
    RuntimeField field;
    field.Name = "rotation";
    field.Offset = offsetof(Turned, Rotation);
    field.Size = sizeof(float);
    field.Scalar = FieldScalar::Float;
    field.Count = 4;
    field.DisplayAngles = true;
    return field;
}
} // namespace

TEST(RuntimeFieldText, ARotationReadsAndWritesAsPitchYawRollInDegrees)
{
    Turned turned;
    const RuntimeField field = RotationField();
    EXPECT_EQ(FormatRuntimeField(field, &turned), "0, 0, 0");

    ASSERT_TRUE(ParseRuntimeField(field, "10, 90, -20", &turned));
    const Quatf stored(turned.Rotation[0], turned.Rotation[1], turned.Rotation[2], turned.Rotation[3]);
    const float degree = std::numbers::pi_v<float> / 180.0f;
    EXPECT_NEAR(std::abs(stored.Dot(Quatf::FromPitchYawRoll(10 * degree, 90 * degree, -20 * degree))), 1.0f, 1e-5f);
    EXPECT_EQ(FormatRuntimeFieldElement(field, &turned, 1), "90");

    ASSERT_TRUE(OffsetRuntimeFieldElement(field, 1, 15.0, &turned));
    EXPECT_EQ(FormatRuntimeField(field, &turned), "10, 105, -20") << "yaw moved, pitch and roll kept";
    EXPECT_FALSE(ParseRuntimeField(field, "10, 20", &turned)) << "three angles, no fewer";
}
