#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/BinaryArchive.h>
#include <core/serialization/JsonArchive.h>
#include <gtest/gtest.h>
#include <logic/TriggerPrograms.h>
#include <logic/TriggerSerializer.h>
#include <sstream>
#include <world/ComponentRegistrar.h>
#include <world/build/EntityBuildPackage.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <zone/ZonePackageImporter.h>

namespace
{
class TriggerSerializerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        Serializer = MakeTriggerSerializer();
        Serializer->RegisterStorage(Scene);
        Entity = Scene.Components.CreateEntity();
    }
    JsonValue Unknown()
    {
        return *JsonParse(
            R"({"version":1,"conditions":[{"query":"game.unknown","arguments":{"target":{"input":"activator"}}}],
            "commands":[{"verb":"game.unknown","arguments":{"Target":{"input":"activator"},"Rich":{"const":{"nested":[null,true,"Mode",17.25]}}},"future_metadata":["a","b"]}],"future_root":{"keep":true}})");
    }
    JsonValue Save()
    {
        JsonWriteArchive out;
        SceneSerializationContext context(Logging);
        EXPECT_TRUE(Serializer->Save(out, Entity, Scene, context));
        return out.TakeValue();
    }
    void Load(const JsonValue &value)
    {
        JsonReadArchive in(value);
        SceneSerializationContext context(Logging);
        ASSERT_TRUE(Serializer->Load(in, Entity, Scene, context));
    }
    LoggingProvider Logging;
    Registry Scene;
    EntityId Entity;
    std::unique_ptr<IComponentSerializer> Serializer;
};
} // namespace

TEST_F(TriggerSerializerTest, MissingProgramCreatesAValidEmptyInlineProgram)
{
    Load(JsonValue::Object{});
    const auto *trigger = std::as_const(Scene.Components).TryGet<Trigger>(Entity);
    ASSERT_NE(trigger, nullptr);
    EXPECT_TRUE(trigger->Enabled);
    EXPECT_TRUE(trigger->Program.IsValid());
    ASSERT_NE(TriggerProgramSource(Scene.Components, Entity), nullptr);
    EXPECT_EQ(JsonStringify(*TriggerProgramSource(Scene.Components, Entity)), JsonStringify(EmptyTriggerProgram()));
}
TEST_F(TriggerSerializerTest, UnknownCommandsAndArgumentsRoundTripWithoutVocabulary)
{
    Load(JsonValue::Object{{"program", Unknown()}, {"cooldown", 0.15}, {"event", "Exit"}});
    const auto saved = Save();
    EXPECT_EQ(JsonStringify(*saved.Find("program")), JsonStringify(Unknown()));
    EXPECT_EQ(saved.Find("event")->AsString(), "Exit");
    EXPECT_NEAR(saved.Find("cooldown")->AsNumber(), 0.15, 0.00001);
    Load(saved);
    EXPECT_EQ(JsonStringify(Save()), JsonStringify(saved));
    EXPECT_EQ(TriggerProgramsOf(Scene.Components).Size(), 1u);
}
TEST_F(TriggerSerializerTest, ExistingEntityReloadDoesNotLeakItsPreviousProgram)
{
    Load(JsonValue::Object{{"program", Unknown()}});
    const auto handle = Scene.Components.TryGet<Trigger>(Entity)->Program;
    Load(JsonValue::Object{{"program", EmptyTriggerProgram()}});
    EXPECT_EQ(Scene.Components.TryGet<Trigger>(Entity)->Program, handle);
    EXPECT_EQ(TriggerProgramsOf(Scene.Components).Size(), 1u);
    ASSERT_TRUE(Serializer->Remove(Entity, Scene));
    EXPECT_EQ(TriggerProgramsOf(Scene.Components).Size(), 0u);
}
TEST_F(TriggerSerializerTest, LegacyBinarySceneChunkRoundTripsInlineValues)
{
    Load(JsonValue::Object{{"program", Unknown()}});
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    BinaryWriter writer(stream);
    BinaryWriteArchive out(writer);
    SceneSerializationContext context(Logging);
    ASSERT_TRUE(Serializer->Save(out, Entity, Scene, context));
    stream.seekg(0);
    BinaryReader reader(stream);
    BinaryReadArchive in(reader);
    ASSERT_TRUE(Serializer->Load(in, Entity, Scene, context));
    EXPECT_EQ(JsonStringify(*TriggerProgramSource(Scene.Components, Entity)), JsonStringify(Unknown()));
    EXPECT_EQ(TriggerProgramsOf(Scene.Components).Size(), 1u);
}
TEST_F(TriggerSerializerTest, SmapPayloadImportsIntoAnotherWorldThroughTheNormalPackagePath)
{
    Load(JsonValue::Object{{"program", Unknown()}, {"once", true}});
    ComponentSerializerRegistry serializers;
    ASSERT_EQ(serializers.Register(MakeTriggerSerializer()), ComponentSerializerRegistry::RegisterResult::Added);
    SmapContents contents;
    contents.Entities.emplace_back().Components.emplace_back(ResolveComponentTypeId<Trigger>(), Save());
    SmapError error;
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSmap(contents, serializers, bytes, &error)) << error.Message;
    SmapContents decoded;
    ASSERT_TRUE(ReadSmap(bytes, serializers, decoded, &error)) << error.Message;
    EntityBuildPackage package;
    ASSERT_TRUE(BuildEntityPackageFromSmap(decoded, serializers, package, &error)) << error.Message;
    WorldComponentSchema schema;
    schema.Add<Trigger>();
    schema.Seal();
    World target;
    schema.Apply(target);
    SceneSerializationContext context(Logging);
    ZoneImportError importError;
    std::vector<EntityId> created;
    ASSERT_TRUE(ImportPackageIntoPartition(target, schema, package, StoragePartitionId::Default(), {}, serializers,
                                           context, &importError, &created))
        << importError.Message;
    ASSERT_EQ(created.size(), 1u);
    const auto *trigger = std::as_const(target).TryGet<Trigger>(created[0]);
    ASSERT_NE(trigger, nullptr);
    EXPECT_TRUE(trigger->Once);
    ASSERT_NE(TriggerProgramSource(target, created[0]), nullptr);
    EXPECT_EQ(JsonStringify(*TriggerProgramSource(target, created[0])), JsonStringify(Unknown()));
    target.DestroyEntity(created[0]);
    EXPECT_EQ(TriggerProgramsOf(target).Size(), 0u);
}

TEST_F(TriggerSerializerTest, NormalSceneSaveAndLoadPreserveUnknownInlineContent)
{
    Load(JsonValue::Object{{"program", Unknown()}});
    ComponentSerializerRegistry serializers;
    ASSERT_EQ(serializers.Register(MakeTriggerSerializer()), ComponentSerializerRegistry::RegisterResult::Added);
    SceneSerializationContext context(Logging);
    const auto document = SaveSceneJson(Scene, serializers, context);
    Registry loaded;
    SceneLoadError error;
    ASSERT_TRUE(LoadSceneJson(document, loaded, serializers, context, &error)) << error.Message;
    const auto entities = loaded.Components.GetAliveEntities();
    ASSERT_EQ(entities.size(), 1u);
    ASSERT_NE(TriggerProgramSource(loaded.Components, entities[0]), nullptr);
    EXPECT_EQ(JsonStringify(*TriggerProgramSource(loaded.Components, entities[0])), JsonStringify(Unknown()));
}
