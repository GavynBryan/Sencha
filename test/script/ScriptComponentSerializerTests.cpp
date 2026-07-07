#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/RuntimeSchema.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/ComponentId.h>
#include <ecs/World.h>
#include <script/ScriptComponentSerializer.h>
#include <script/ScriptModule.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

//=============================================================================
// ScriptComponentSerializer synthesizes an IComponentSerializer from a cooked
// script's component schema. These tests drive that synthesis directly from a
// hand-built module: the reflected fields the inspector renders, the default
// image, a save/load round-trip of the supported scalars, and the documented
// v1 behavior for unsupported and absent fields.
//=============================================================================

namespace
{
    // Offsets and layout a cooker would emit: bool@0, i32@4, f32@8, f64@16,
    // entity@24; alignment 8, size 32.
    constexpr std::uint16_t kFlagOffset = 0;
    constexpr std::uint16_t kCountOffset = 4;
    constexpr std::uint16_t kFreqOffset = 8;
    constexpr std::uint16_t kAmpOffset = 16;
    constexpr std::uint16_t kTargetOffset = 24;
    constexpr std::size_t kComponentSize = 32;

    std::uint32_t AddString(ScriptModule& module, std::string_view text)
    {
        module.Strings.emplace_back(text);
        return static_cast<std::uint32_t>(module.Strings.size() - 1);
    }

    void AddField(ScriptModule& module, ScriptComponentDef& def, std::string_view name,
                  ScriptScalarKind scalar, std::uint16_t offset, std::uint64_t defaultRaw)
    {
        ScriptComponentField field;
        field.Name = AddString(module, name);
        field.Scalar = static_cast<std::uint8_t>(scalar);
        field.ArrayCount = 1;
        field.ByteOffset = offset;
        field.DefaultRaw = defaultRaw;
        def.Fields.push_back(field);
    }

    // A module declaring one component "Config" with a bool, i32, f32, f64, and
    // an (unsupported) Entity field, each with a distinct default.
    ScriptModule MakeConfigModule()
    {
        ScriptModule module;
        module.Strings.emplace_back(""); // index 0 is the empty string
        ScriptComponentDef def;
        def.Name = AddString(module, "Config");
        // DefaultRaw holds each field's default in storage (scalar) format, in
        // the low bytes, exactly as the cooker emits it: an f32 keeps its 32-bit
        // pattern, not the VM's f64 register form.
        AddField(module, def, "flag", ScriptScalarKind::Bool, kFlagOffset, 1);
        AddField(module, def, "count", ScriptScalarKind::Int32, kCountOffset, 7);
        AddField(module, def, "freq", ScriptScalarKind::Float, kFreqOffset,
                 std::bit_cast<std::uint32_t>(2.0f));
        AddField(module, def, "amp", ScriptScalarKind::Double, kAmpOffset,
                 std::bit_cast<std::uint64_t>(60.0));
        AddField(module, def, "target", ScriptScalarKind::Entity, kTargetOffset, 0);
        module.Components.push_back(std::move(def));
        return module;
    }

    template <typename T>
    T ReadAt(const std::byte* bytes, std::size_t offset)
    {
        T value{};
        std::memcpy(&value, bytes + offset, sizeof(T));
        return value;
    }
}

TEST(ScriptComponentSerializer, RuntimeFieldsMirrorSchema)
{
    const ScriptModule module = MakeConfigModule();
    ScriptComponentSerializer serializer(module, module.Components[0]);

    const std::span<const RuntimeField> fields = serializer.RuntimeFields();
    ASSERT_EQ(fields.size(), 5u);

    EXPECT_EQ(fields[0].Name, "flag");
    EXPECT_EQ(fields[0].Offset, kFlagOffset);
    EXPECT_EQ(fields[0].Size, 1u);
    EXPECT_EQ(fields[0].Scalar, FieldScalar::Bool);
    EXPECT_FALSE(fields[0].ReadOnly);

    EXPECT_EQ(fields[1].Name, "count");
    EXPECT_EQ(fields[1].Offset, kCountOffset);
    EXPECT_EQ(fields[1].Scalar, FieldScalar::Int32);

    EXPECT_EQ(fields[2].Name, "freq");
    EXPECT_EQ(fields[2].Offset, kFreqOffset);
    EXPECT_EQ(fields[2].Scalar, FieldScalar::Float);

    EXPECT_EQ(fields[3].Name, "amp");
    EXPECT_EQ(fields[3].Offset, kAmpOffset);
    EXPECT_EQ(fields[3].Scalar, FieldScalar::Double);

    // Entity is not expressible as an inspector scalar: shown, read-only, not edited.
    EXPECT_EQ(fields[4].Name, "target");
    EXPECT_EQ(fields[4].Scalar, FieldScalar::Unsupported);
    EXPECT_TRUE(fields[4].ReadOnly);
}

TEST(ScriptComponentSerializer, DefaultBytesHonorDeclaredDefaults)
{
    const ScriptModule module = MakeConfigModule();
    ScriptComponentSerializer serializer(module, module.Components[0]);

    const std::vector<std::byte> bytes = serializer.DefaultBytes();
    ASSERT_EQ(bytes.size(), kComponentSize);

    EXPECT_EQ(ReadAt<std::uint8_t>(bytes.data(), kFlagOffset), 1u);
    EXPECT_EQ(ReadAt<std::uint32_t>(bytes.data(), kCountOffset), 7u);
    EXPECT_FLOAT_EQ(ReadAt<float>(bytes.data(), kFreqOffset), 2.0f);
    EXPECT_DOUBLE_EQ(ReadAt<double>(bytes.data(), kAmpOffset), 60.0);
}

TEST(ScriptComponentSerializer, RoundTripsSupportedScalarsThroughJson)
{
    const ScriptModule module = MakeConfigModule();
    ScriptComponentSerializer serializer(module, module.Components[0]);

    LoggingProvider logging;
    SceneSerializationContext context(logging);

    // Author an entity carrying the component with values distinct from defaults.
    Registry saveWorld;
    serializer.RegisterStorage(saveWorld);
    const ComponentId id = saveWorld.Components.GetComponentIdByType(serializer.TypeId());
    ASSERT_NE(id, InvalidComponentId);
    const EntityId entity = saveWorld.Components.CreateEntity();

    std::vector<std::byte> image = serializer.DefaultBytes();
    const std::uint8_t flag = 0;
    std::memcpy(image.data() + kFlagOffset, &flag, 1);
    const std::uint32_t count = 42;
    std::memcpy(image.data() + kCountOffset, &count, 4);
    const float freq = 3.5f;
    std::memcpy(image.data() + kFreqOffset, &freq, 4);
    const double amp = 123.0;
    std::memcpy(image.data() + kAmpOffset, &amp, 8);
    const std::uint64_t target = 0xABCDu; // unsupported field: deliberately non-default
    std::memcpy(image.data() + kTargetOffset, &target, 8);
    saveWorld.Components.AddComponentRaw(entity, id, image.data(), kComponentSize,
                                         /*align*/ 8, /*onAdd*/ nullptr);

    // Save produces the component's own object, exactly as SaveSceneJson keys it.
    JsonWriteArchive writer;
    ASSERT_TRUE(serializer.Save(writer, entity, saveWorld, context));
    const JsonValue json = writer.TakeValue();

    // Load into a fresh world exactly as scene load would.
    Registry loadWorld;
    serializer.RegisterStorage(loadWorld);
    const ComponentId loadId = loadWorld.Components.GetComponentIdByType(serializer.TypeId());
    const EntityId loaded = loadWorld.Components.CreateEntity();

    JsonReadArchive reader(json);
    ASSERT_TRUE(serializer.Load(reader, loaded, loadWorld, context));

    const void* raw = loadWorld.Components.GetComponentRaw(loaded, loadId);
    ASSERT_NE(raw, nullptr);
    const auto* bytes = static_cast<const std::byte*>(raw);

    EXPECT_EQ(ReadAt<std::uint8_t>(bytes, kFlagOffset), 0u);
    EXPECT_EQ(ReadAt<std::uint32_t>(bytes, kCountOffset), 42u);
    EXPECT_FLOAT_EQ(ReadAt<float>(bytes, kFreqOffset), 3.5f);
    EXPECT_DOUBLE_EQ(ReadAt<double>(bytes, kAmpOffset), 123.0);
    // The unsupported field is not persisted, so it comes back at its default.
    EXPECT_EQ(ReadAt<std::uint64_t>(bytes, kTargetOffset), 0u);
}

TEST(ScriptComponentSerializer, AbsentFieldsKeepDefaultsOnLoad)
{
    const ScriptModule module = MakeConfigModule();
    ScriptComponentSerializer serializer(module, module.Components[0]);

    LoggingProvider logging;
    SceneSerializationContext context(logging);

    // A component object carrying only "count": an older or partial record. The
    // empty-key BeginObject matches the wrapper the serializer reads through.
    JsonWriteArchive writer;
    writer.BeginObject(std::string_view{});
    writer.Field("count", std::uint32_t{ 99 });
    writer.End();
    const JsonValue json = writer.TakeValue();

    Registry world;
    serializer.RegisterStorage(world);
    const ComponentId id = world.Components.GetComponentIdByType(serializer.TypeId());
    const EntityId entity = world.Components.CreateEntity();

    JsonReadArchive reader(json);
    ASSERT_TRUE(serializer.Load(reader, entity, world, context));

    const auto* bytes = static_cast<const std::byte*>(world.Components.GetComponentRaw(entity, id));
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(ReadAt<std::uint32_t>(bytes, kCountOffset), 99u);        // present: read
    EXPECT_EQ(ReadAt<std::uint8_t>(bytes, kFlagOffset), 1u);           // absent: default
    EXPECT_FLOAT_EQ(ReadAt<float>(bytes, kFreqOffset), 2.0f);          // absent: default
    EXPECT_DOUBLE_EQ(ReadAt<double>(bytes, kAmpOffset), 60.0);         // absent: default
}
