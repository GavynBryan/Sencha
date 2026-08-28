#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZonePackageImporter.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

//-----------------------------------------------------------------------------
// Test components. SmapSkewValue deliberately reuses SmapTestValue's stable
// name with a different field set: same ComponentTypeId, different schema
// fingerprint -- the exact skew the format must refuse.
//-----------------------------------------------------------------------------

struct SmapTestValue
{
    int Value = 0;
    float Scale = 1.0f;
};

template <>
struct TypeSchema<SmapTestValue>
{
    static constexpr std::string_view Name = "smap_test_value";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('S', 'M', 'T', 'V');

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &SmapTestValue::Value),
            MakeField("scale", &SmapTestValue::Scale),
        };
    }
};

struct SmapSkewValue
{
    int Value = 0;
};

template <>
struct TypeSchema<SmapSkewValue>
{
    static constexpr std::string_view Name = "smap_test_value";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('S', 'M', 'T', 'V');

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &SmapSkewValue::Value),
        };
    }
};

namespace
{

ComponentSerializerRegistry MakeSerializers()
{
    ComponentSerializerRegistry serializers;
    EXPECT_EQ(serializers.Register(
                  std::make_unique<ComponentSerializer<SmapTestValue>>()),
              ComponentSerializerRegistry::RegisterResult::Added);
    EXPECT_EQ(serializers.Register(
                  std::make_unique<ComponentSerializer<PersistentIdComponent>>()),
              ComponentSerializerRegistry::RegisterResult::Added);
    return serializers;
}

JsonValue TestValuePayload(double value, double scale)
{
    return JsonValue(JsonValue::Object{
        { "value", JsonValue(value) },
        { "scale", JsonValue(scale) },
    });
}

JsonValue PersistentIdPayload(const std::string& hex)
{
    return JsonValue(JsonValue::Object{ { "id", JsonValue(hex) } });
}

// Two entities (child parented to root), a persistent id on the root, one
// dependency, one collision cell -- every section populated.
SmapContents MakeContents()
{
    SmapContents contents;
    contents.Dependencies.push_back(SmapDependency{
        AssetId{ 77 }, AssetType::StaticMesh, "asset://meshes/crate.smesh" });
    contents.Collision.push_back(
        SmapCollisionCell{ ".cooked/levels/zone.scol", Vec3d(1.0f, 2.0f, 3.0f) });

    SmapEntityRecord root;
    root.Persistent = PersistentEntityId{ 0xaa };
    root.Components.emplace_back(ResolveComponentTypeId<SmapTestValue>(),
                                 TestValuePayload(11.0, 0.5));
    root.Components.emplace_back(ResolveComponentTypeId<PersistentIdComponent>(),
                                 PersistentIdPayload("00000000000000aa"));
    contents.Entities.push_back(std::move(root));

    SmapEntityRecord child;
    child.Parent = 0;
    child.Components.emplace_back(ResolveComponentTypeId<SmapTestValue>(),
                                  TestValuePayload(22.0, 2.0));
    contents.Entities.push_back(std::move(child));
    return contents;
}

std::vector<std::byte> WriteOrDie(const SmapContents& contents,
                                  const ComponentSerializerRegistry& serializers)
{
    std::vector<std::byte> bytes;
    SmapError error;
    EXPECT_TRUE(WriteSmap(contents, serializers, bytes, &error)) << error.Message;
    return bytes;
}

} // namespace

TEST(SmapFormat, RoundTripPreservesEverySection)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    const SmapContents written = MakeContents();
    const std::vector<std::byte> bytes = WriteOrDie(written, serializers);

    SmapContents read;
    SmapError error;
    ASSERT_TRUE(ReadSmap(bytes, serializers, read, &error)) << error.Message;

    ASSERT_EQ(read.Dependencies.size(), 1u);
    EXPECT_EQ(read.Dependencies[0].Id, (AssetId{ 77 }));
    EXPECT_EQ(read.Dependencies[0].Type, AssetType::StaticMesh);
    EXPECT_EQ(read.Dependencies[0].Path, "asset://meshes/crate.smesh");

    ASSERT_EQ(read.Collision.size(), 1u);
    EXPECT_EQ(read.Collision[0].BlobPath, ".cooked/levels/zone.scol");
    EXPECT_EQ(read.Collision[0].Origin.X, 1.0f);
    EXPECT_EQ(read.Collision[0].Origin.Y, 2.0f);
    EXPECT_EQ(read.Collision[0].Origin.Z, 3.0f);

    ASSERT_EQ(read.Entities.size(), 2u);
    EXPECT_EQ(read.Entities[0].Persistent, (PersistentEntityId{ 0xaa }));
    EXPECT_EQ(read.Entities[0].Parent, UINT32_MAX);
    EXPECT_EQ(read.Entities[1].Parent, 0u);
    ASSERT_EQ(read.Entities[1].Components.size(), 1u);
    EXPECT_EQ(read.Entities[1].Components[0].first,
              ResolveComponentTypeId<SmapTestValue>());

    const JsonValue& payload = read.Entities[1].Components[0].second;
    ASSERT_TRUE(payload.IsObject());
    ASSERT_NE(payload.Find("value"), nullptr);
    EXPECT_EQ(payload.Find("value")->AsNumber(), 22.0);
    ASSERT_NE(payload.Find("scale"), nullptr);
    EXPECT_EQ(payload.Find("scale")->AsNumber(), 2.0);
}

TEST(SmapFormat, WriteIsByteDeterministicAndCanonicalThroughDecode)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    const SmapContents contents = MakeContents();

    const std::vector<std::byte> first = WriteOrDie(contents, serializers);
    const std::vector<std::byte> second = WriteOrDie(contents, serializers);
    EXPECT_EQ(first, second);

    // Decode and re-encode: the canonical form must survive the round trip,
    // or "did this recook change anything" becomes unanswerable byte-wise.
    SmapContents read;
    ASSERT_TRUE(ReadSmap(first, serializers, read, nullptr));
    const std::vector<std::byte> third = WriteOrDie(read, serializers);
    EXPECT_EQ(first, third);
}

TEST(SmapFormat, SchemaFingerprintMismatchRefusesNamingTheComponent)
{
    const ComponentSerializerRegistry writers = MakeSerializers();
    const std::vector<std::byte> bytes = WriteOrDie(MakeContents(), writers);

    // A build whose smap_test_value serializer lost a field: same identity,
    // different shape.
    ComponentSerializerRegistry readers;
    ASSERT_EQ(readers.Register(std::make_unique<ComponentSerializer<SmapSkewValue>>()),
              ComponentSerializerRegistry::RegisterResult::Added);
    ASSERT_EQ(readers.Register(
                  std::make_unique<ComponentSerializer<PersistentIdComponent>>()),
              ComponentSerializerRegistry::RegisterResult::Added);

    SmapContents read;
    SmapError error;
    EXPECT_FALSE(ReadSmap(bytes, readers, read, &error));
    EXPECT_NE(error.Message.find("smap_test_value"), std::string::npos)
        << error.Message;
    EXPECT_NE(error.Message.find("recook"), std::string::npos) << error.Message;
}

TEST(SmapFormat, UnknownComponentRefusedAtWriteAndAtRead)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();

    SmapContents unknown;
    SmapEntityRecord record;
    record.Components.emplace_back(MakeComponentTypeId("nobody.registers_this"),
                                   JsonValue(JsonValue::Object{}));
    unknown.Entities.push_back(std::move(record));

    std::vector<std::byte> bytes;
    SmapError writeError;
    EXPECT_FALSE(WriteSmap(unknown, serializers, bytes, &writeError));
    EXPECT_NE(writeError.Message.find("no registered serializer"),
              std::string::npos)
        << writeError.Message;

    // A valid file read by a build missing the component's serializer.
    const std::vector<std::byte> valid = WriteOrDie(MakeContents(), serializers);
    ComponentSerializerRegistry empty;
    SmapContents read;
    SmapError readError;
    EXPECT_FALSE(ReadSmap(valid, empty, read, &readError));
    EXPECT_NE(readError.Message.find("does not register"), std::string::npos)
        << readError.Message;
}

TEST(SmapFormat, EveryTruncationAndEveryByteFlipIsRefused)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    const std::vector<std::byte> bytes = WriteOrDie(MakeContents(), serializers);

    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
        SmapContents read;
        EXPECT_FALSE(ReadSmap(std::span(bytes.data(), length), serializers, read,
                              nullptr))
            << "truncation to " << length << " bytes was accepted";
    }

    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        std::vector<std::byte> corrupt = bytes;
        corrupt[i] ^= std::byte{ 0xFF };
        SmapContents read;
        EXPECT_FALSE(ReadSmap(corrupt, serializers, read, nullptr))
            << "flip at byte " << i << " was accepted";
    }
}

TEST(SmapFormat, WriterRefusesParentCyclesAndOutOfRangeParents)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();

    SmapContents cycle;
    cycle.Entities.emplace_back().Parent = 1;
    cycle.Entities.emplace_back().Parent = 0;
    std::vector<std::byte> bytes;
    SmapError error;
    EXPECT_FALSE(WriteSmap(cycle, serializers, bytes, &error));
    EXPECT_NE(error.Message.find("cycle"), std::string::npos) << error.Message;

    SmapContents range;
    range.Entities.emplace_back().Parent = 7;
    EXPECT_FALSE(WriteSmap(range, serializers, bytes, &error));
    EXPECT_NE(error.Message.find("past the last entity"), std::string::npos)
        << error.Message;
}

TEST(SmapFormat, WriterRefusesIdentityDisagreement)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    std::vector<std::byte> bytes;
    SmapError error;

    // Recorded id with no persistent_id component.
    SmapContents missing;
    missing.Entities.emplace_back().Persistent = PersistentEntityId{ 0xbb };
    EXPECT_FALSE(WriteSmap(missing, serializers, bytes, &error));
    EXPECT_NE(error.Message.find("disagrees"), std::string::npos) << error.Message;

    // Recorded id contradicting the component.
    SmapContents wrong = MakeContents();
    wrong.Entities[0].Persistent = PersistentEntityId{ 0xbb };
    EXPECT_FALSE(WriteSmap(wrong, serializers, bytes, &error));
    EXPECT_NE(error.Message.find("disagrees"), std::string::npos) << error.Message;
}

TEST(SmapFormat, PackageBuildCarriesIdentityParentsAndPayloads)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    const std::vector<std::byte> bytes = WriteOrDie(MakeContents(), serializers);

    SmapContents read;
    ASSERT_TRUE(ReadSmap(bytes, serializers, read, nullptr));

    EntityBuildPackage package;
    SmapError error;
    ASSERT_TRUE(BuildEntityPackageFromSmap(read, serializers, package, &error))
        << error.Message;

    ASSERT_EQ(package.EntityCount(), 2u);
    EXPECT_EQ(package.Entities()[0].PersistentId, (PersistentEntityId{ 0xaa }));
    EXPECT_FALSE(package.Entities()[1].PersistentId.IsValid());
    ASSERT_EQ(package.Parents().size(), 1u);
    EXPECT_EQ(package.Parents()[0].Child, (PackageEntityId{ 1 }));
    EXPECT_EQ(package.Parents()[0].Parent, (PackageEntityId{ 0 }));

    // Payloads travel as serialized JSON for owner-thread decode, the same
    // form the JSON package builder produced.
    ASSERT_EQ(package.Entities()[1].Components.size(), 1u);
    const PackageComponent& component = package.Entities()[1].Components[0];
    EXPECT_FALSE(component.HasRuntimeBytes());
    ASSERT_TRUE(component.SerializedJson.has_value());
    ASSERT_NE(component.SerializedJson->Find("value"), nullptr);
    EXPECT_EQ(component.SerializedJson->Find("value")->AsNumber(), 22.0);
}

TEST(SmapFormat, ImportsIntoRuntimeWorldLikeTheJsonPath)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    const std::vector<std::byte> bytes = WriteOrDie(MakeContents(), serializers);

    SmapContents read;
    ASSERT_TRUE(ReadSmap(bytes, serializers, read, nullptr));
    EntityBuildPackage package;
    ASSERT_TRUE(BuildEntityPackageFromSmap(read, serializers, package, nullptr));

    WorldComponentSchema schema;
    schema.Add<Parent>();
    schema.Add<SmapTestValue>();
    schema.Add<PersistentIdComponent>();
    schema.Seal();
    RuntimeWorld runtime(schema);

    LoggingProvider logging;
    SceneSerializationContext context(logging);
    ZoneImportError importError;
    ASSERT_TRUE(ImportZonePackage(runtime, schema, ZoneId{ 12 }, package,
                                  serializers, context,
                                  ZoneParticipation{ .Logic = true },
                                  &importError))
        << importError.Message;

    EntityId root;
    EntityId child;
    for (EntityId entity : runtime.Entities().GetAliveEntities())
    {
        const SmapTestValue* value =
            runtime.Entities().TryGet<SmapTestValue>(entity);
        if (value == nullptr)
            continue;
        if (value->Value == 11)
            root = entity;
        else if (value->Value == 22)
            child = entity;
    }
    ASSERT_TRUE(root.IsValid());
    ASSERT_TRUE(child.IsValid());

    const Parent* parent = runtime.Entities().TryGet<Parent>(child);
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(parent->Entity, root);

    const PersistentIdComponent* persistent =
        runtime.Entities().TryGet<PersistentIdComponent>(root);
    ASSERT_NE(persistent, nullptr);
    EXPECT_EQ(persistent->Id, (PersistentEntityId{ 0xaa }));
}
