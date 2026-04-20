#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <core/metadata/EnumSchema.h>
#include <core/metadata/Field.h>
#include <core/metadata/SchemaVisit.h>
#include <core/metadata/TypeSchema.h>
#include <core/json/JsonValue.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <core/serialization/Serialize.h>
#include <core/serialization/BinaryFormat.h>
#include <math/MathSchemas.h>

// Helper: create a binary read/write stringstream.
static std::stringstream MakeBinaryStream()
{
    return std::stringstream(std::ios::in | std::ios::out | std::ios::binary);
}

enum class SchemaTestKind
{
    First,
    Second
};

template <>
struct EnumSchema<SchemaTestKind>
{
    static constexpr std::array Values = {
        EnumValue{ SchemaTestKind::First, "first" },
        EnumValue{ SchemaTestKind::Second, "second" },
    };
};

struct SchemaTestRecord
{
    std::uint32_t Id = 0;
    float Weight = 0.0f;
    bool Enabled = false;
    SchemaTestKind Kind = SchemaTestKind::First;
    std::uint32_t OptionalCount = 0;

    bool operator==(const SchemaTestRecord&) const = default;
};

template <>
struct TypeSchema<SchemaTestRecord>
{
    static constexpr std::string_view Name = "SchemaTestRecord";

    static auto Fields()
    {
        return std::tuple{
            MakeField("id", &SchemaTestRecord::Id),
            MakeField("weight", &SchemaTestRecord::Weight),
            MakeField("enabled", &SchemaTestRecord::Enabled),
            MakeField("kind", &SchemaTestRecord::Kind),
            MakeField("optional_count", &SchemaTestRecord::OptionalCount).Default(std::uint32_t{ 77 }),
        };
    }
};

struct EnumOnlyRecord
{
    SchemaTestKind Kind = SchemaTestKind::First;
};

template <>
struct TypeSchema<EnumOnlyRecord>
{
    static constexpr std::string_view Name = "EnumOnlyRecord";

    static auto Fields()
    {
        return std::tuple{
            MakeField("kind", &EnumOnlyRecord::Kind),
        };
    }
};

struct FieldNameCollector
{
    std::vector<std::string_view>& Names;

    template <typename FieldT, typename T>
    void Field(const FieldT& field, const T&)
    {
        Names.push_back(field.Name);
    }
};

TEST(SerializationTests, SchemaTraversalVisitsFieldsInDeclaredOrder)
{
    SchemaTestRecord record{};
    std::vector<std::string_view> names;
    FieldNameCollector visitor{ names };

    VisitSchema<TypeSchema<SchemaTestRecord>>(record, visitor);

    ASSERT_EQ(names.size(), 5u);
    EXPECT_EQ(names[0], "id");
    EXPECT_EQ(names[1], "weight");
    EXPECT_EQ(names[2], "enabled");
    EXPECT_EQ(names[3], "kind");
    EXPECT_EQ(names[4], "optional_count");
}

TEST(SerializationTests, SchemaBinaryRoundTripsStandaloneRecord)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const SchemaTestRecord expected{
        .Id = 42,
        .Weight = 3.5f,
        .Enabled = true,
        .Kind = SchemaTestKind::Second,
        .OptionalCount = 9,
    };
    ASSERT_TRUE(Serialize(writer, expected));

    stream.seekg(0);
    BinaryReader reader(stream);

    SchemaTestRecord actual{};
    ASSERT_TRUE(Deserialize(reader, actual));
    EXPECT_EQ(actual, expected);
}

TEST(SerializationTests, SchemaJsonRoundTripsStandaloneRecordAndIgnoresUnknownFields)
{
    const JsonValue json(JsonValue::Object{
        { "id", JsonValue(42) },
        { "weight", JsonValue(3.5) },
        { "enabled", JsonValue(true) },
        { "kind", JsonValue("second") },
        { "optional_count", JsonValue(9) },
        { "unknown", JsonValue("ignored") },
    });

    SchemaTestRecord actual{};
    ASSERT_TRUE(FromJson(json, actual));

    EXPECT_EQ(actual.Id, 42u);
    EXPECT_FLOAT_EQ(actual.Weight, 3.5f);
    EXPECT_TRUE(actual.Enabled);
    EXPECT_EQ(actual.Kind, SchemaTestKind::Second);
    EXPECT_EQ(actual.OptionalCount, 9u);

    JsonValue roundTrip = ToJson(actual);
    ASSERT_TRUE(roundTrip.IsObject());
    EXPECT_NE(roundTrip.Find("id"), nullptr);
    EXPECT_EQ(roundTrip.Find("unknown"), nullptr);
}

TEST(SerializationTests, OptionalJsonFieldMissingAssignsDeclaredDefault)
{
    const JsonValue json(JsonValue::Object{
        { "id", JsonValue(12) },
        { "weight", JsonValue(1.25) },
        { "enabled", JsonValue(false) },
        { "kind", JsonValue("first") },
    });

    SchemaTestRecord actual{};
    ASSERT_TRUE(FromJson(json, actual));
    EXPECT_EQ(actual.OptionalCount, 77u);
}

TEST(SerializationTests, EnumWritesAsStringInJsonAndIntegerInBinary)
{
    const EnumOnlyRecord expected{ .Kind = SchemaTestKind::Second };

    JsonValue json = ToJson(expected);
    ASSERT_TRUE(json.IsObject());
    const JsonValue* kind = json.Find("kind");
    ASSERT_NE(kind, nullptr);
    ASSERT_TRUE(kind->IsString());
    EXPECT_EQ(kind->AsString(), "second");

    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(Serialize(writer, expected));

    stream.seekg(0);
    BinaryReader reader(stream);
    std::uint32_t rawKind = 0;
    ASSERT_TRUE(Deserialize(reader, rawKind));
    EXPECT_EQ(rawKind, static_cast<std::uint32_t>(SchemaTestKind::Second));
}

TEST(SerializationTests, MathSchemasPreserveJsonShapes)
{
    JsonValue vec = ToJson(Vec3d(1.0f, 2.0f, 3.0f));
    ASSERT_TRUE(vec.IsArray());
    ASSERT_EQ(vec.AsArray().size(), 3u);
    EXPECT_DOUBLE_EQ(vec.AsArray()[0].AsNumber(), 1.0);
    EXPECT_DOUBLE_EQ(vec.AsArray()[1].AsNumber(), 2.0);
    EXPECT_DOUBLE_EQ(vec.AsArray()[2].AsNumber(), 3.0);

    JsonValue transform = ToJson(Transform3f(
        Vec3d(1.0f, 2.0f, 3.0f),
        Quatf::Identity(),
        Vec3d(4.0f, 5.0f, 6.0f)));
    ASSERT_TRUE(transform.IsObject());
    EXPECT_NE(transform.Find("position"), nullptr);
    EXPECT_NE(transform.Find("rotation"), nullptr);
    EXPECT_NE(transform.Find("scale"), nullptr);
}

//=============================================================================
// Phase 1 â€” Primitive round-trips
//=============================================================================

TEST(SerializationTests, RoundTripsPrimitiveValues)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const std::uint32_t expectedU32 = 150;
    const float expectedF = 42.5f;
    const double expectedD = -13.25;
    const bool expectedB = true;
    const std::int16_t expectedI16 = -512;

    ASSERT_TRUE(Serialize(writer, expectedU32));
    ASSERT_TRUE(Serialize(writer, expectedF));
    ASSERT_TRUE(Serialize(writer, expectedD));
    ASSERT_TRUE(Serialize(writer, expectedB));
    ASSERT_TRUE(Serialize(writer, expectedI16));

    stream.seekg(0);
    BinaryReader reader(stream);

    std::uint32_t actualU32 = 0;
    float actualF = 0.0f;
    double actualD = 0.0;
    bool actualB = false;
    std::int16_t actualI16 = 0;

    ASSERT_TRUE(Deserialize(reader, actualU32));
    ASSERT_TRUE(Deserialize(reader, actualF));
    ASSERT_TRUE(Deserialize(reader, actualD));
    ASSERT_TRUE(Deserialize(reader, actualB));
    ASSERT_TRUE(Deserialize(reader, actualI16));

    EXPECT_EQ(actualU32, expectedU32);
    EXPECT_FLOAT_EQ(actualF, expectedF);
    EXPECT_DOUBLE_EQ(actualD, expectedD);
    EXPECT_EQ(actualB, expectedB);
    EXPECT_EQ(actualI16, expectedI16);
}

TEST(SerializationTests, ReadFailsWhenStreamIsTruncated)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const std::uint32_t health = 123;
    ASSERT_TRUE(Serialize(writer, health));

    stream.seekg(0);
    BinaryReader reader(stream);

    std::uint32_t readHealth = 0;
    float readX = 0.0f;

    ASSERT_TRUE(Deserialize(reader, readHealth));
    EXPECT_FALSE(Deserialize(reader, readX));
}

//=============================================================================
// Phase 1 â€” Record round-trip (explicit per-field serialization)
//=============================================================================

struct PlayerRecord
{
    std::uint32_t Health = 0;
    float X = 0.0f;
    float Y = 0.0f;
};

static bool Serialize(BinaryWriter& writer, const PlayerRecord& r)
{
    return Serialize(writer, r.Health)
        && Serialize(writer, r.X)
        && Serialize(writer, r.Y);
}

static bool Deserialize(BinaryReader& reader, PlayerRecord& r)
{
    return Deserialize(reader, r.Health)
        && Deserialize(reader, r.X)
        && Deserialize(reader, r.Y);
}

struct AssetId
{
    std::uint32_t Value = 0;

    bool operator==(const AssetId&) const = default;
    explicit operator bool() const { return Value != 0; }
};

struct TypeId
{
    std::uint32_t Value = 0;

    bool operator==(const TypeId&) const = default;
    explicit operator bool() const { return Value != 0; }
};

struct EntityId
{
    std::uint32_t Value = 0;

    bool operator==(const EntityId&) const = default;
    explicit operator bool() const { return Value != 0; }
};

static bool Serialize(BinaryWriter& writer, const AssetId& id) { return writer.Write(id.Value); }
static bool Deserialize(BinaryReader& reader, AssetId& id) { return reader.Read(id.Value); }

static bool Serialize(BinaryWriter& writer, const TypeId& id) { return writer.Write(id.Value); }
static bool Deserialize(BinaryReader& reader, TypeId& id) { return reader.Read(id.Value); }

static bool Serialize(BinaryWriter& writer, const EntityId& id) { return writer.Write(id.Value); }
static bool Deserialize(BinaryReader& reader, EntityId& id) { return reader.Read(id.Value); }

TEST(SerializationTests, RoundTripsPlayerRecord)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const PlayerRecord expected{ .Health = 99, .X = 10.5f, .Y = -2.25f };
    ASSERT_TRUE(Serialize(writer, expected));

    stream.seekg(0);
    BinaryReader reader(stream);

    PlayerRecord actual{};
    ASSERT_TRUE(Deserialize(reader, actual));

    EXPECT_EQ(actual.Health, expected.Health);
    EXPECT_FLOAT_EQ(actual.X, expected.X);
    EXPECT_FLOAT_EQ(actual.Y, expected.Y);
}

//=============================================================================
// Phase 2 â€” String round-trip
//=============================================================================

TEST(SerializationTests, RoundTripsString)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const std::string expected = "Hello, Sencha!";
    ASSERT_TRUE(Serialize(writer, expected));

    stream.seekg(0);
    BinaryReader reader(stream);

    std::string actual;
    ASSERT_TRUE(Deserialize(reader, actual));
    EXPECT_EQ(actual, expected);
}

TEST(SerializationTests, RoundTripsEmptyString)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const std::string expected;
    ASSERT_TRUE(Serialize(writer, expected));

    stream.seekg(0);
    BinaryReader reader(stream);

    std::string actual = "garbage";
    ASSERT_TRUE(Deserialize(reader, actual));
    EXPECT_TRUE(actual.empty());
}

TEST(SerializationTests, BoundedStringRejectsTooLong)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const std::string longStr = "This string is too long";
    ASSERT_TRUE(Serialize(writer, longStr));

    stream.seekg(0);
    BinaryReader reader(stream);

    std::string actual;
    EXPECT_FALSE(Deserialize(reader, actual, 5));
}

//=============================================================================
// Phase 2 â€” Trivial array round-trip
//=============================================================================

TEST(SerializationTests, RoundTripsTrivialArray)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const std::vector<std::uint32_t> expected = { 10, 20, 30, 40, 50 };
    ASSERT_TRUE(SerializeTrivialArray(writer, expected));

    stream.seekg(0);
    BinaryReader reader(stream);

    std::vector<std::uint32_t> actual;
    ASSERT_TRUE(DeserializeTrivialArray(reader, actual));
    EXPECT_EQ(actual, expected);
}

TEST(SerializationTests, RoundTripsEmptyTrivialArray)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const std::vector<float> expected;
    ASSERT_TRUE(SerializeTrivialArray(writer, expected));

    stream.seekg(0);
    BinaryReader reader(stream);

    std::vector<float> actual = { 1.0f, 2.0f };
    ASSERT_TRUE(DeserializeTrivialArray(reader, actual));
    EXPECT_TRUE(actual.empty());
}

TEST(SerializationTests, BoundedTrivialArrayRejectsTooMany)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const std::vector<std::uint32_t> data = { 1, 2, 3, 4, 5 };
    ASSERT_TRUE(SerializeTrivialArray(writer, data));

    stream.seekg(0);
    BinaryReader reader(stream);

    std::vector<std::uint32_t> actual;
    EXPECT_FALSE(DeserializeTrivialArray(reader, actual, 3));
}

//=============================================================================
// Phase 2 â€” Per-element array round-trip (vector of records)
//=============================================================================

struct InventoryItemRecord
{
    AssetId ItemAsset;
    std::uint32_t Quantity = 0;
};

static bool SerializeItem(BinaryWriter& writer, const InventoryItemRecord& item)
{
    return Serialize(writer, item.ItemAsset)
        && Serialize(writer, item.Quantity);
}

static bool DeserializeItem(BinaryReader& reader, InventoryItemRecord& item)
{
    return Deserialize(reader, item.ItemAsset)
        && Deserialize(reader, item.Quantity);
}

TEST(SerializationTests, RoundTripsArrayOfRecords)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const std::vector<InventoryItemRecord> expected = {
        { .ItemAsset = { 101 }, .Quantity = 5 },
        { .ItemAsset = { 202 }, .Quantity = 12 },
        { .ItemAsset = { 303 }, .Quantity = 1 },
    };

    ASSERT_TRUE(SerializeArray(writer, expected, SerializeItem));

    stream.seekg(0);
    BinaryReader reader(stream);

    std::vector<InventoryItemRecord> actual;
    ASSERT_TRUE(DeserializeArray(reader, actual, DeserializeItem));

    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        EXPECT_EQ(actual[i].ItemAsset, expected[i].ItemAsset);
        EXPECT_EQ(actual[i].Quantity, expected[i].Quantity);
    }
}

TEST(SerializationTests, BoundedArrayOfRecordsRejectsTooMany)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const std::vector<InventoryItemRecord> data = {
        { .ItemAsset = { 1 }, .Quantity = 1 },
        { .ItemAsset = { 2 }, .Quantity = 2 },
        { .ItemAsset = { 3 }, .Quantity = 3 },
    };

    ASSERT_TRUE(SerializeArray(writer, data, SerializeItem));

    stream.seekg(0);
    BinaryReader reader(stream);

    std::vector<InventoryItemRecord> actual;
    EXPECT_FALSE(DeserializeArray(reader, actual, DeserializeItem, 2));
}

//=============================================================================
// Phase 3 â€” BinaryHeader
//=============================================================================

constexpr std::uint32_t TestMagic   = 0x54455354; // "TEST" as little-endian
constexpr std::uint32_t TestVersion = 1;

TEST(SerializationTests, RoundTripsBinaryHeader)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    ASSERT_TRUE(WriteBinaryHeader(writer, TestMagic, TestVersion));

    stream.seekg(0);
    BinaryReader reader(stream);

    BinaryHeader header{};
    ASSERT_TRUE(ReadBinaryHeader(reader, header));
    EXPECT_TRUE(ValidateBinaryHeader(header, TestMagic, TestVersion));
}

TEST(SerializationTests, HeaderValidationFailsOnWrongMagic)
{
    BinaryHeader header{ .Magic = 0xDEADBEEF, .Version = TestVersion };
    EXPECT_FALSE(ValidateBinaryHeader(header, TestMagic, TestVersion));
}

TEST(SerializationTests, HeaderValidationFailsOnWrongVersion)
{
    BinaryHeader header{ .Magic = TestMagic, .Version = 99 };
    EXPECT_FALSE(ValidateBinaryHeader(header, TestMagic, TestVersion));
}

//=============================================================================
// Phase 3 â€” ChunkHeader (raw)
//=============================================================================

TEST(SerializationTests, RoundTripsChunkHeader)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const ChunkHeader expected{ .Id = 42, .Version = 1, .Size = 128 };
    ASSERT_TRUE(WriteChunkHeader(writer, expected));

    stream.seekg(0);
    BinaryReader reader(stream);

    ChunkHeader actual{};
    ASSERT_TRUE(ReadChunkHeader(reader, actual));
    EXPECT_EQ(actual.Id, expected.Id);
    EXPECT_EQ(actual.Version, expected.Version);
    EXPECT_EQ(actual.Size, expected.Size);
}

//=============================================================================
// Phase 3 â€” ChunkWriter / ChunkReader
//=============================================================================

TEST(SerializationTests, ChunkWriterPatchesPayloadSize)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    ChunkWriter chunk;
    ASSERT_TRUE(chunk.Begin(writer, 1, 1));

    const float x = 1.0f;
    const float y = 2.0f;
    ASSERT_TRUE(writer.Write(x));
    ASSERT_TRUE(writer.Write(y));

    ASSERT_TRUE(chunk.End(writer));

    // Verify: read back and check the size was patched correctly.
    stream.seekg(0);
    BinaryReader reader(stream);

    ChunkHeader header{};
    ASSERT_TRUE(ReadChunkHeader(reader, header));
    EXPECT_EQ(header.Id, 1u);
    EXPECT_EQ(header.Version, 1u);
    EXPECT_EQ(header.Size, 2 * sizeof(float));
}

TEST(SerializationTests, ChunkReaderSkipsUnknownChunk)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    // Write two chunks: one "unknown" and one "known".
    ChunkWriter unknownChunk;
    ASSERT_TRUE(unknownChunk.Begin(writer, 99, 1));
    const std::uint32_t junk = 0xDEADBEEF;
    ASSERT_TRUE(writer.Write(junk));
    ASSERT_TRUE(unknownChunk.End(writer));

    ChunkWriter knownChunk;
    ASSERT_TRUE(knownChunk.Begin(writer, 1, 1));
    const float value = 42.5f;
    ASSERT_TRUE(writer.Write(value));
    ASSERT_TRUE(knownChunk.End(writer));

    // Read back: skip the unknown chunk, read the known one.
    stream.seekg(0);
    BinaryReader reader(stream);

    ChunkReader firstChunk;
    ASSERT_TRUE(firstChunk.ReadHeader(reader));
    EXPECT_EQ(firstChunk.GetHeader().Id, 99u);
    ASSERT_TRUE(firstChunk.Skip(reader));

    ChunkReader secondChunk;
    ASSERT_TRUE(secondChunk.ReadHeader(reader));
    EXPECT_EQ(secondChunk.GetHeader().Id, 1u);

    float readValue = 0.0f;
    ASSERT_TRUE(reader.Read(readValue));
    EXPECT_FLOAT_EQ(readValue, value);
}

//=============================================================================
// Phase 4 â€” Identity types round-trip
//=============================================================================

TEST(SerializationTests, RoundTripsIdTypes)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    const AssetId  expectedAsset  { 42 };
    const TypeId   expectedType   { 7 };
    const EntityId expectedEntity { 1001 };

    ASSERT_TRUE(Serialize(writer, expectedAsset));
    ASSERT_TRUE(Serialize(writer, expectedType));
    ASSERT_TRUE(Serialize(writer, expectedEntity));

    stream.seekg(0);
    BinaryReader reader(stream);

    AssetId  actualAsset{};
    TypeId   actualType{};
    EntityId actualEntity{};

    ASSERT_TRUE(Deserialize(reader, actualAsset));
    ASSERT_TRUE(Deserialize(reader, actualType));
    ASSERT_TRUE(Deserialize(reader, actualEntity));

    EXPECT_EQ(actualAsset, expectedAsset);
    EXPECT_EQ(actualType, expectedType);
    EXPECT_EQ(actualEntity, expectedEntity);
}

TEST(SerializationTests, DefaultIdIsFalsy)
{
    AssetId id{};
    EXPECT_FALSE(static_cast<bool>(id));
    EXPECT_TRUE(static_cast<bool>(AssetId{ 1 }));
}

//=============================================================================
// Phase 5 â€” Tagged heterogeneous payload example
//
// Demonstrates serializing a list of components with different types using
// explicit type IDs and per-component dispatch. No reflection, no registry,
// no ISerializable â€” just a switch statement.
//=============================================================================

// Component type IDs (would live in a shared header in production code).
constexpr TypeId HealthComponentTypeId  { 1 };
constexpr TypeId PositionComponentTypeId{ 2 };

struct HealthComponent
{
    std::uint32_t Current = 0;
    std::uint32_t Max     = 0;
};

struct PositionComponent
{
    float X = 0.0f;
    float Y = 0.0f;
};

// Per-component serialize (each writes TypeId + payload size + payload).
static bool SerializeHealthComponent(BinaryWriter& writer, const HealthComponent& c)
{
    ChunkWriter chunk;
    if (!chunk.Begin(writer, HealthComponentTypeId.Value, 1)) return false;
    if (!Serialize(writer, c.Current)) return false;
    if (!Serialize(writer, c.Max))     return false;
    return chunk.End(writer);
}

static bool SerializePositionComponent(BinaryWriter& writer, const PositionComponent& c)
{
    ChunkWriter chunk;
    if (!chunk.Begin(writer, PositionComponentTypeId.Value, 1)) return false;
    if (!Serialize(writer, c.X)) return false;
    if (!Serialize(writer, c.Y)) return false;
    return chunk.End(writer);
}

// A complete actor record with heterogeneous components.
struct ActorRecord
{
    EntityId Id;
    // For simplicity: optional components stored as has-flags.
    bool HasHealth   = false;
    bool HasPosition = false;
    HealthComponent   Health;
    PositionComponent Position;
};

static bool SerializeActorRecord(BinaryWriter& writer, const ActorRecord& actor)
{
    if (!Serialize(writer, actor.Id)) return false;

    // Write component count so the reader knows when to stop.
    std::uint32_t componentCount = (actor.HasHealth ? 1 : 0)
                                 + (actor.HasPosition ? 1 : 0);
    if (!Serialize(writer, componentCount)) return false;

    if (actor.HasHealth)
    {
        if (!SerializeHealthComponent(writer, actor.Health)) return false;
    }
    if (actor.HasPosition)
    {
        if (!SerializePositionComponent(writer, actor.Position)) return false;
    }
    return true;
}

static bool DeserializeActorRecord(BinaryReader& reader, ActorRecord& actor)
{
    if (!Deserialize(reader, actor.Id)) return false;

    std::uint32_t componentCount = 0;
    if (!Deserialize(reader, componentCount)) return false;

    for (std::uint32_t i = 0; i < componentCount; ++i)
    {
        ChunkReader chunk;
        if (!chunk.ReadHeader(reader)) return false;

        const auto& header = chunk.GetHeader();

        if (header.Id == HealthComponentTypeId.Value)
        {
            actor.HasHealth = true;
            if (!Deserialize(reader, actor.Health.Current)) return false;
            if (!Deserialize(reader, actor.Health.Max))     return false;
        }
        else if (header.Id == PositionComponentTypeId.Value)
        {
            actor.HasPosition = true;
            if (!Deserialize(reader, actor.Position.X)) return false;
            if (!Deserialize(reader, actor.Position.Y)) return false;
        }
        else
        {
            // Unknown component â€” skip it for forward compatibility.
            if (!chunk.Skip(reader)) return false;
        }
    }
    return true;
}

TEST(SerializationTests, RoundTripsTaggedHeterogeneousComponents)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    ActorRecord expected;
    expected.Id          = EntityId{ 42 };
    expected.HasHealth   = true;
    expected.Health      = { .Current = 75, .Max = 100 };
    expected.HasPosition = true;
    expected.Position    = { .X = 10.5f, .Y = -3.25f };

    ASSERT_TRUE(SerializeActorRecord(writer, expected));

    stream.seekg(0);
    BinaryReader reader(stream);

    ActorRecord actual{};
    ASSERT_TRUE(DeserializeActorRecord(reader, actual));

    EXPECT_EQ(actual.Id, expected.Id);
    EXPECT_TRUE(actual.HasHealth);
    EXPECT_EQ(actual.Health.Current, 75u);
    EXPECT_EQ(actual.Health.Max, 100u);
    EXPECT_TRUE(actual.HasPosition);
    EXPECT_FLOAT_EQ(actual.Position.X, 10.5f);
    EXPECT_FLOAT_EQ(actual.Position.Y, -3.25f);
}

TEST(SerializationTests, DeserializerSkipsUnknownComponents)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    // Manually write an actor with an unknown component type between known ones.
    ASSERT_TRUE(Serialize(writer, EntityId{ 7 }));
    ASSERT_TRUE(Serialize(writer, std::uint32_t(3))); // 3 components

    // Health component
    ASSERT_TRUE(SerializeHealthComponent(writer, { .Current = 50, .Max = 50 }));

    // Unknown component (type 999)
    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(writer, 999, 1));
        ASSERT_TRUE(writer.Write(std::uint64_t(0xCAFEBABE)));
        ASSERT_TRUE(chunk.End(writer));
    }

    // Position component
    ASSERT_TRUE(SerializePositionComponent(writer, { .X = 1.0f, .Y = 2.0f }));

    stream.seekg(0);
    BinaryReader reader(stream);

    ActorRecord actual{};
    ASSERT_TRUE(DeserializeActorRecord(reader, actual));

    EXPECT_EQ(actual.Id, EntityId{ 7 });
    EXPECT_TRUE(actual.HasHealth);
    EXPECT_EQ(actual.Health.Current, 50u);
    EXPECT_TRUE(actual.HasPosition);
    EXPECT_FLOAT_EQ(actual.Position.X, 1.0f);
    EXPECT_FLOAT_EQ(actual.Position.Y, 2.0f);
}

//=============================================================================
// Phase 6 â€” Full-format example (inventory with header + chunks)
//=============================================================================

struct InventoryRecord
{
    std::uint32_t Gold = 0;
    std::vector<InventoryItemRecord> Items;
};

constexpr std::uint32_t InventoryMagic   = 0x494E5654; // "INVT"
constexpr std::uint32_t InventoryVersion = 1;
constexpr std::uint32_t GoldChunkId      = 1;
constexpr std::uint32_t ItemsChunkId     = 2;

static bool SerializeInventory(BinaryWriter& writer, const InventoryRecord& inv)
{
    if (!WriteBinaryHeader(writer, InventoryMagic, InventoryVersion)) return false;

    // Gold chunk
    {
        ChunkWriter chunk;
        if (!chunk.Begin(writer, GoldChunkId, 1)) return false;
        if (!Serialize(writer, inv.Gold)) return false;
        if (!chunk.End(writer)) return false;
    }

    // Items chunk
    {
        ChunkWriter chunk;
        if (!chunk.Begin(writer, ItemsChunkId, 1)) return false;
        if (!SerializeArray(writer, inv.Items, SerializeItem)) return false;
        if (!chunk.End(writer)) return false;
    }

    return true;
}

static bool DeserializeInventory(BinaryReader& reader, InventoryRecord& inv)
{
    BinaryHeader header{};
    if (!ReadBinaryHeader(reader, header)) return false;
    if (!ValidateBinaryHeader(header, InventoryMagic, InventoryVersion)) return false;

    // Read chunks in order (known layout).
    {
        ChunkReader chunk;
        if (!chunk.ReadHeader(reader)) return false;
        if (chunk.GetHeader().Id != GoldChunkId) return false;
        if (!Deserialize(reader, inv.Gold)) return false;
    }

    {
        ChunkReader chunk;
        if (!chunk.ReadHeader(reader)) return false;
        if (chunk.GetHeader().Id != ItemsChunkId) return false;
        if (!DeserializeArray(reader, inv.Items, DeserializeItem)) return false;
    }

    return true;
}

TEST(SerializationTests, RoundTripsFullInventoryFormat)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);

    InventoryRecord expected;
    expected.Gold = 1500;
    expected.Items = {
        { .ItemAsset = { 10 }, .Quantity = 3 },
        { .ItemAsset = { 20 }, .Quantity = 1 },
    };

    ASSERT_TRUE(SerializeInventory(writer, expected));

    stream.seekg(0);
    BinaryReader reader(stream);

    InventoryRecord actual{};
    ASSERT_TRUE(DeserializeInventory(reader, actual));

    EXPECT_EQ(actual.Gold, 1500u);
    ASSERT_EQ(actual.Items.size(), 2u);
    EXPECT_EQ(actual.Items[0].ItemAsset, AssetId{ 10 });
    EXPECT_EQ(actual.Items[0].Quantity, 3u);
    EXPECT_EQ(actual.Items[1].ItemAsset, AssetId{ 20 });
    EXPECT_EQ(actual.Items[1].Quantity, 1u);
}
