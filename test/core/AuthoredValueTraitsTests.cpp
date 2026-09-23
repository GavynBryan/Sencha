// How an ordinary C++ type crosses the authored boundary: the kind it
// declares, what decodes into it, what it refuses, and which of its values can
// be stated as a schema default.

#include <authored/AuthoredValueTraits.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace
{
enum class TraitsMood : std::uint8_t
{
    Calm,
    Angry,
    // Deliberately missing from the schema.
    Unlisted,
};
} // namespace

template<>
struct EnumSchema<TraitsMood>
{
    static constexpr std::array Values = {
        EnumValue{ TraitsMood::Calm, "calm", "Calm" },
        EnumValue{ TraitsMood::Angry, "angry", "Angry" },
    };
};

namespace
{
template<typename T>
[[nodiscard]] DataFieldSchema Describe()
{
    DataFieldSchema field;
    AuthoredValueTraits<T>::Describe(field);
    return field;
}

template<typename T>
[[nodiscard]] bool RoundTrips(const T& value)
{
    T decoded{};
    return AuthoredValueTraits<T>::Decode(AuthoredValueTraits<T>::Encode(value), decoded)
        && decoded == value;
}
} // namespace

TEST(AuthoredValueTraits, EachSupportedTypeDeclaresItsKind)
{
    EXPECT_EQ(Describe<bool>().Kind, DataFieldKind::Bool);
    EXPECT_EQ(Describe<std::int64_t>().Kind, DataFieldKind::Int);
    EXPECT_EQ(Describe<std::uint8_t>().Kind, DataFieldKind::Int);
    EXPECT_EQ(Describe<float>().Kind, DataFieldKind::Float);
    EXPECT_EQ(Describe<double>().Kind, DataFieldKind::Float);
    EXPECT_EQ(Describe<std::string>().Kind, DataFieldKind::String);
    EXPECT_EQ(Describe<TraitsMood>().Kind, DataFieldKind::Enum);
    EXPECT_EQ(Describe<EntityId>().Kind, DataFieldKind::Entity);
    EXPECT_EQ(Describe<GameplayTagId>().Kind, DataFieldKind::GameplayTag);
    EXPECT_EQ(Describe<AssetRef>().Kind, DataFieldKind::AssetRef);

    using Vec3 = Vec<3, float>;
    const DataFieldSchema vector = Describe<Vec3>();
    EXPECT_EQ(vector.Kind, DataFieldKind::Vector);
    EXPECT_EQ(vector.VectorLength, 3u);

    const DataFieldSchema optional = Describe<std::optional<EntityId>>();
    EXPECT_EQ(optional.Kind, DataFieldKind::Optional);
    EXPECT_FALSE(optional.Required);
    ASSERT_EQ(optional.Children.size(), 1u);
    EXPECT_EQ(optional.Children.front().Kind, DataFieldKind::Entity);
}

TEST(AuthoredValueTraits, TheIntegerContractIsSixtyFourBitSigned)
{
    static_assert(IsAuthoredValueType<std::int8_t>);
    static_assert(IsAuthoredValueType<std::int64_t>);
    static_assert(IsAuthoredValueType<std::uint32_t>);
    // Values the authored integer cannot hold exactly are not authored values.
    static_assert(!IsAuthoredValueType<std::uint64_t>);
    static_assert(!IsAuthoredValueType<std::size_t>);
    // Text, not numbers.
    static_assert(!IsAuthoredValueType<char>);

    // A narrow type states its range, so content cannot author what it would
    // refuse.
    const DataFieldSchema narrow = Describe<std::int8_t>();
    EXPECT_EQ(narrow.Numeric.Minimum, -128.0);
    EXPECT_EQ(narrow.Numeric.Maximum, 127.0);
    EXPECT_FALSE(Describe<std::int64_t>().Numeric.Minimum.has_value());
}

TEST(AuthoredValueTraits, ValuesRoundTrip)
{
    EXPECT_TRUE(RoundTrips(true));
    EXPECT_TRUE(RoundTrips(std::int64_t{ -7 }));
    EXPECT_TRUE(RoundTrips(std::uint32_t{ 4000000000u }));
    EXPECT_TRUE(RoundTrips(2.5f));
    EXPECT_TRUE(RoundTrips(std::string("hello")));
    EXPECT_TRUE(RoundTrips(TraitsMood::Angry));
    EXPECT_TRUE(RoundTrips(EntityId{ .Index = 3, .Generation = 2 }));
    EXPECT_TRUE(RoundTrips(GameplayTagId{ 9 }));
    using Vec2 = Vec<2, double>;
    EXPECT_TRUE(RoundTrips(Vec2{ 1.0, -2.0 }));
    EXPECT_TRUE(RoundTrips(std::optional<std::int32_t>{ 5 }));
    EXPECT_TRUE(RoundTrips(std::optional<std::int32_t>{}));
}

TEST(AuthoredValueTraits, NarrowingIsRefusedNotWrapped)
{
    std::int8_t small = 0;
    EXPECT_FALSE(AuthoredValueTraits<std::int8_t>::Decode(AuthoredValue::Int(128), small));
    EXPECT_FALSE(AuthoredValueTraits<std::int8_t>::Decode(AuthoredValue::Int(-129), small));
    EXPECT_TRUE(AuthoredValueTraits<std::int8_t>::Decode(AuthoredValue::Int(-128), small));
    EXPECT_EQ(small, -128);

    std::int32_t medium = 0;
    EXPECT_FALSE(AuthoredValueTraits<std::int32_t>::Decode(
        AuthoredValue::Int(std::int64_t{ std::numeric_limits<std::int32_t>::max() } + 1), medium));

    std::uint32_t unsigned32 = 0;
    EXPECT_FALSE(AuthoredValueTraits<std::uint32_t>::Decode(AuthoredValue::Int(-1), unsigned32));

    float single = 0.0f;
    EXPECT_FALSE(AuthoredValueTraits<float>::Decode(AuthoredValue::Float(1e300), single));
    EXPECT_FALSE(AuthoredValueTraits<float>::Decode(
        AuthoredValue::Float(std::numeric_limits<double>::infinity()), single));
    // Precision only is rounded.
    EXPECT_TRUE(AuthoredValueTraits<float>::Decode(AuthoredValue::Float(0.1), single));
}

TEST(AuthoredValueTraits, AWrongKindOrAnUnlistedChoiceIsRefused)
{
    std::int64_t whole = 0;
    EXPECT_FALSE(AuthoredValueTraits<std::int64_t>::Decode(AuthoredValue::Float(1.0), whole));
    bool flag = false;
    EXPECT_FALSE(AuthoredValueTraits<bool>::Decode(AuthoredValue::Int(1), flag));

    TraitsMood mood = TraitsMood::Calm;
    EXPECT_FALSE(AuthoredValueTraits<TraitsMood>::Decode(AuthoredValue::Enum("sulky"), mood));
    // An enumerator the schema does not list has no authored name.
    EXPECT_TRUE(AuthoredValueTraits<TraitsMood>::Encode(TraitsMood::Unlisted).IsNone());

    // Absent decodes into an optional, and into nothing else.
    std::optional<EntityId> maybe = EntityId{ .Index = 1 };
    EXPECT_TRUE(AuthoredValueTraits<std::optional<EntityId>>::Decode(AuthoredValue{}, maybe));
    EXPECT_FALSE(maybe.has_value());
    EntityId entity;
    EXPECT_FALSE(AuthoredValueTraits<EntityId>::Decode(AuthoredValue{}, entity));

    using Vec3 = Vec<3, float>;
    Vec3 vector;
    AuthoredVectorValue two;
    two.Length = 2;
    EXPECT_FALSE(AuthoredValueTraits<Vec3>::Decode(AuthoredValue::Vector(two), vector));
}

TEST(AuthoredValueTraits, OnlyValuesAJsonDocumentCanSpellAreSchemaDefaults)
{
    static_assert(CanRepresentSchemaDefault<bool>);
    static_assert(CanRepresentSchemaDefault<std::int32_t>);
    static_assert(CanRepresentSchemaDefault<float>);
    static_assert(CanRepresentSchemaDefault<std::string>);
    static_assert(CanRepresentSchemaDefault<TraitsMood>);
    static_assert(CanRepresentSchemaDefault<std::optional<std::int32_t>>);
    static_assert(!CanRepresentSchemaDefault<EntityId>);
    static_assert(!CanRepresentSchemaDefault<GameplayTagId>);
    static_assert(!CanRepresentSchemaDefault<AssetRef>);
    static_assert(!CanRepresentSchemaDefault<Vec<3, float>>);

    EXPECT_EQ(AuthoredSchemaDefault<std::int32_t>::ToDefault(4), DataDefaultValue(std::int64_t{ 4 }));
    EXPECT_EQ(AuthoredSchemaDefault<float>::ToDefault(0.5f), DataDefaultValue(0.5));
    EXPECT_EQ(AuthoredSchemaDefault<TraitsMood>::ToDefault(TraitsMood::Angry),
              DataDefaultValue(std::string("angry")));
    EXPECT_EQ(AuthoredSchemaDefault<std::optional<std::int32_t>>::ToDefault(std::nullopt),
              DataDefaultValue{});
}

TEST(AuthoredValueTraits, ARangeMustFitTheTypeItConstrains)
{
    static_assert(AuthoredRangeFits<std::int8_t>(-128, 127));
    static_assert(!AuthoredRangeFits<std::int8_t>(0, 200));
    static_assert(!AuthoredRangeFits<std::int32_t>(10, 1));
    static_assert(AuthoredRangeFits<std::optional<float>>(0.0, 1.0));
    static_assert(!AuthoredRangeFits<std::string>(0, 1));
    SUCCEED();
}
