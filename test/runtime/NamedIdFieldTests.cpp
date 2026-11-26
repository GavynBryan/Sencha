#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/Field.h>
#include <core/serialization/BinaryArchive.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <core/serialization/FourCC.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/ItemDefinition.h>
#include <runtime/spawn/SceneSpawnOverlay.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/ZonePackageImporter.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

struct NamedFieldsProbe
{
    GameplayTagId Required;
    GameplayTagId Optional;
    ItemId Item;
};

template <>
struct TypeSchema<NamedFieldsProbe>
{
    static constexpr std::string_view Name = "NamedFieldsProbe";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('N', 'F', 'P', 'R');
    static auto Fields()
    {
        return std::tuple{ MakeField("required", &NamedFieldsProbe::Required),
                           MakeField("optional", &NamedFieldsProbe::Optional).Optional(),
                           MakeField("item", &NamedFieldsProbe::Item).Optional() };
    }
};

namespace
{
    struct MessageSink final : ILogSink
    {
        void Write(LogLevel, std::string_view, std::string_view message) override { Messages.emplace_back(message); }

        bool Said(std::string_view text) const
        {
            return std::ranges::any_of(Messages, [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }

        std::vector<std::string> Messages;
    };

    struct ProbeWorld
    {
        explicit ProbeWorld(std::initializer_list<std::string_view> tagsInOrder)
            : Context(Logging)
            , Sink(Logging.AddSink<MessageSink>())
        {
            Entities.RegisterComponent<NamedFieldsProbe>();
            GameplayTagRegistry& tags = Entities.AddResource<GameplayTagRegistry>();
            for (const std::string_view tag : tagsInOrder)
                (void)tags.RegisterTag(tag);
            Entities.AddResource<ItemRegistry>();
        }

        GameplayTagId Tag(std::string_view name) { return Entities.GetResource<GameplayTagRegistry>().FindTag(name); }
        ItemId Item(std::string_view name) { return Entities.GetResource<ItemRegistry>().Reserve(name); }

        const NamedFieldsProbe* Probe(EntityId entity) const { return Entities.TryGet<NamedFieldsProbe>(entity); }

        LoggingProvider Logging;
        SceneSerializationContext Context;
        MessageSink& Sink;
        World Entities;
        ComponentSerializer<NamedFieldsProbe> Serializer;
    };

    std::string SavedJson(ProbeWorld& world, EntityId entity)
    {
        JsonWriteArchive archive;
        EXPECT_TRUE(world.Serializer.Save(archive, entity, world.Entities, world.Context));
        return JsonStringify(archive.TakeValue());
    }

    bool LoadedJson(ProbeWorld& world, EntityId entity, std::string_view text)
    {
        const std::optional<JsonValue> parsed = JsonParse(text);
        EXPECT_TRUE(parsed.has_value());
        JsonReadArchive archive(*parsed);
        return world.Serializer.LoadIntoWorld(archive, entity, world.Entities, world.Context);
    }
}

TEST(NamedIdField, RequiredAndOptionalTagsAndAnItemSaveAsNamesAndLoadInAnotherRegistrationOrder)
{
    ProbeWorld source({ "state.armed", "state.cold" });
    const EntityId entity = source.Entities.CreateEntity();
    source.Entities.AddComponent<NamedFieldsProbe>(
        entity, NamedFieldsProbe{ .Required = source.Tag("state.armed"), .Optional = source.Tag("state.cold"),
                                  .Item = source.Item("asset://items/medkit.sdata") });

    const std::string saved = SavedJson(source, entity);
    EXPECT_EQ(saved, R"({"required":"state.armed","optional":"state.cold","item":"asset://items/medkit.sdata"})");

    ProbeWorld loaded({ "state.cold", "state.armed" });
    const EntityId into = loaded.Entities.CreateEntity();
    ASSERT_TRUE(LoadedJson(loaded, into, saved));
    const NamedFieldsProbe* probe = loaded.Probe(into);
    ASSERT_NE(probe, nullptr);
    EXPECT_EQ(probe->Required, loaded.Tag("state.armed"));
    EXPECT_EQ(probe->Optional, loaded.Tag("state.cold"));
    EXPECT_EQ(probe->Item, loaded.Item("asset://items/medkit.sdata"));
}

TEST(NamedIdField, AnUnsetOptionalNameSavesAsAbsentAndLoadsBackUnset)
{
    ProbeWorld world({ "state.armed" });
    const EntityId entity = world.Entities.CreateEntity();
    world.Entities.AddComponent<NamedFieldsProbe>(
        entity, NamedFieldsProbe{ .Required = world.Tag("state.armed"), .Optional = {}, .Item = {} });

    const std::string saved = SavedJson(world, entity);
    EXPECT_EQ(saved, R"({"required":"state.armed"})");
    const EntityId into = world.Entities.CreateEntity();
    ASSERT_TRUE(LoadedJson(world, into, saved));
    EXPECT_FALSE(world.Probe(into)->Optional.IsValid());
    EXPECT_FALSE(world.Probe(into)->Item.IsValid());
}

TEST(NamedIdField, NamesRoundTripThroughTheBinaryArchive)
{
    ProbeWorld source({ "state.armed", "state.cold" });
    const EntityId set = source.Entities.CreateEntity();
    source.Entities.AddComponent<NamedFieldsProbe>(
        set, NamedFieldsProbe{ .Required = source.Tag("state.armed"), .Optional = source.Tag("state.cold"),
                               .Item = source.Item("asset://items/medkit.sdata") });
    const EntityId unset = source.Entities.CreateEntity();
    source.Entities.AddComponent<NamedFieldsProbe>(
        unset, NamedFieldsProbe{ .Required = source.Tag("state.cold"), .Optional = {}, .Item = {} });

    std::stringstream bytes(std::ios::in | std::ios::out | std::ios::binary);
    {
        BinaryWriter writer(bytes);
        BinaryWriteArchive out(writer);
        ASSERT_TRUE(source.Serializer.Save(out, set, source.Entities, source.Context));
        ASSERT_TRUE(source.Serializer.Save(out, unset, source.Entities, source.Context));
    }
    bytes.seekg(0);
    ProbeWorld loaded({ "state.cold", "state.armed" });
    BinaryReader reader(bytes);
    BinaryReadArchive in(reader);
    const EntityId first = loaded.Entities.CreateEntity();
    const EntityId second = loaded.Entities.CreateEntity();
    ASSERT_TRUE(loaded.Serializer.LoadIntoWorld(in, first, loaded.Entities, loaded.Context));
    ASSERT_TRUE(loaded.Serializer.LoadIntoWorld(in, second, loaded.Entities, loaded.Context));

    EXPECT_EQ(loaded.Probe(first)->Required, loaded.Tag("state.armed"));
    EXPECT_EQ(loaded.Probe(first)->Optional, loaded.Tag("state.cold"));
    EXPECT_EQ(loaded.Probe(first)->Item, loaded.Item("asset://items/medkit.sdata"));
    EXPECT_EQ(loaded.Probe(second)->Required, loaded.Tag("state.cold"));
    EXPECT_FALSE(loaded.Probe(second)->Optional.IsValid());
    EXPECT_FALSE(loaded.Probe(second)->Item.IsValid());
}

TEST(NamedIdField, ATagThisContentDoesNotDeclareRefusesTheSceneAndSaysSo)
{
    ProbeWorld world({ "state.armed" });
    const EntityId entity = world.Entities.CreateEntity();

    EXPECT_FALSE(LoadedJson(world, entity, R"({"required":"state.missing"})"));
    EXPECT_EQ(world.Probe(entity), nullptr);
    EXPECT_TRUE(world.Sink.Said("'required' names gameplay tag 'state.missing', which this content does not declare"));
}

TEST(NamedIdField, AnItemNamedBeforeItIsDefinedIsReservedByItsName)
{
    ProbeWorld world({ "state.armed" });
    const EntityId entity = world.Entities.CreateEntity();

    ASSERT_TRUE(LoadedJson(world, entity, R"({"required":"state.armed","item":"asset://items/later.sdata"})"));
    EXPECT_EQ(world.Entities.GetResource<ItemRegistry>().Find("asset://items/later.sdata"), world.Probe(entity)->Item);
    EXPECT_TRUE(world.Probe(entity)->Item.IsValid());
}

TEST(NamedIdField, ARequiredNameLeftOutIsMissing)
{
    ProbeWorld world({ "state.armed" });
    const EntityId entity = world.Entities.CreateEntity();

    EXPECT_FALSE(LoadedJson(world, entity, R"({"optional":"state.armed"})"));
}

TEST(NamedIdField, TheCompositionSchemaNamesTheNamedMembers)
{
    const DataFieldSchema* schema = ComponentSerializer<NamedFieldsProbe>{}.CompositionSchema();
    ASSERT_NE(schema, nullptr);
    std::vector<std::pair<std::string, DataFieldKind>> members;
    for (const DataFieldSchema& child : schema->Children)
        members.emplace_back(child.Key, child.Kind);
    EXPECT_EQ(members, (std::vector<std::pair<std::string, DataFieldKind>>{
                           { "required", DataFieldKind::GameplayTag },
                           { "optional", DataFieldKind::GameplayTag },
                           { "item", DataFieldKind::DataAssetRef } }));
}

TEST(NamedIdField, ABodyPatchNamingATagContentNoLongerDeclaresRestoresContentsValueWithAWarning)
{
    WorldComponentSchema schema;
    schema.Add<NamedFieldsProbe>();
    schema.Seal();
    RuntimeWorld runtime(schema);
    World& world = runtime.Entities();
    const GameplayTagId kept = *world.AddResource<GameplayTagRegistry>().RegisterTag("state.kept");
    world.AddResource<ItemRegistry>();
    ComponentSerializerRegistry serializers;
    (void)serializers.Register(std::make_unique<ComponentSerializer<NamedFieldsProbe>>());
    LoggingProvider logging;
    MessageSink& sink = logging.AddSink<MessageSink>();
    SceneSerializationContext context(logging);

    EntityBuildPackage prefab;
    const PackageEntityId root = prefab.CreateEntity();
    (void)prefab.SetSerializedComponent(root, ResolveComponentTypeId<NamedFieldsProbe>(),
                                        *JsonParse(R"({"required":"state.kept"})"));
    SceneSpawnOverlay overlay;
    overlay.RootPatch = *JsonParse(R"({"NamedFieldsProbe":{"required":"state.deleted"}})");
    std::string error;
    std::vector<std::string> retuned;
    ASSERT_TRUE(ApplySpawnOverlay(prefab, overlay, serializers, error, retuned)) << error;

    ZoneImportError imported;
    std::vector<EntityId> created;
    ASSERT_TRUE(ImportPackageIntoPartition(world, schema, prefab, StoragePartitionId::Default(), ZoneStateScope{},
                                           serializers, context, &imported, &created))
        << imported.Message;
    ASSERT_EQ(created.size(), 1u);
    const NamedFieldsProbe* probe = world.TryGet<NamedFieldsProbe>(created.front());
    ASSERT_NE(probe, nullptr);
    EXPECT_EQ(probe->Required, kept);
    EXPECT_TRUE(sink.Said("does not decode"));
}
