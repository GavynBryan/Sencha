#include <abilities/AbilityGrants.h>
#include <abilities/AbilityKit.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <core/json/JsonEquals.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/WorldComponentSchema.h>
#include <equipment/EquipmentComponents.h>
#include <gameplay_tags/BaselineGameplayTags.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/InventoryComponents.h>
#include <logic/Responses.h>
#include <physics/components/Collider.h>
#include <physics/components/RigidBody.h>
#include <render/Material.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/identity/PersistableRef.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/EntityPatch.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <zone/KeptWithZone.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZoneStateMemory.h>
#include <zone/ZoneStateStore.h>

#include <gtest/gtest.h>

#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <vector>

struct FilterValues
{
    float Speed = 1.0f;
    int Steps = 0;
    PersistentEntityId Home;
};

template <>
struct TypeSchema<FilterValues>
{
    static constexpr std::string_view Name = "filter_values";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('F', 'V', 'A', 'L');
    static auto Fields()
    {
        return std::tuple{ MakeField("speed", &FilterValues::Speed), MakeField("steps", &FilterValues::Steps),
                           MakeField("home", &FilterValues::Home) };
    }
};

template <>
struct ComponentPersistenceOf<FilterValues>
{
    static constexpr ComponentPersistence Value = ComponentPersistence::Saved;
};

struct FilterAsset
{
    MaterialHandle Material;
};

template <>
struct TypeSchema<FilterAsset>
{
    static constexpr std::string_view Name = "filter_asset";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('F', 'A', 'S', 'T');
    static auto Fields() { return std::tuple{ MakeField("material", &FilterAsset::Material).AsAsset(AssetType::Material) }; }
};

struct FilterEntity
{
    PersistableRef Target;
};

template <>
struct TypeSchema<FilterEntity>
{
    static constexpr std::string_view Name = "filter_entity";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('F', 'E', 'N', 'T');
    static auto Fields() { return std::tuple{ MakeField("target", &FilterEntity::Target) }; }
};

struct FilterNamedId
{
    std::uint32_t Value = 0;
};

template <>
struct SceneFieldCodec<FilterNamedId>
{
    static bool Save(IWriteArchive& archive, std::string_view key, FilterNamedId value, SceneSerializationContext&)
    {
        archive.Field(key, std::string_view(value.Value == 0 ? "none" : "named"));
        return archive.Ok();
    }
    static bool Load(IReadArchive& archive, std::string_view key, FilterNamedId& value, SceneSerializationContext&)
    {
        std::string name;
        archive.Field(key, name);
        value.Value = name == "none" ? 0 : 1;
        return archive.Ok();
    }
};

struct FilterNamed
{
    FilterNamedId Id;
};

template <>
struct TypeSchema<FilterNamed>
{
    static constexpr std::string_view Name = "filter_named";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('F', 'N', 'A', 'M');
    static auto Fields() { return std::tuple{ MakeField("id", &FilterNamed::Id) }; }
};

namespace
{
    constexpr ZoneParticipation kLogic{ .Visible = false, .Physics = false, .Logic = true, .Audio = false };
    constexpr ZoneId kHall{ 0x91 };
    const LevelReference kLevel{ .Kind = LevelKind::World, .Name = "filter" };
    constexpr std::uint64_t kFirst = 0x2000;

    class CountingValues final : public IComponentSerializer
    {
    public:
        explicit CountingValues(int& writes)
            : Writes(writes)
        {
        }

        ComponentTypeId TypeId() const override { return Inner.TypeId(); }
        std::string_view JsonKey() const override { return Inner.JsonKey(); }
        std::uint32_t BinaryChunkId() const override { return Inner.BinaryChunkId(); }
        std::span<const RuntimeField> RuntimeFields() const override { return Inner.RuntimeFields(); }
        std::vector<std::byte> DefaultBytes() const override { return Inner.DefaultBytes(); }
        void RegisterStorage(Registry& registry) const override { Inner.RegisterStorage(registry); }
        bool HasComponent(EntityId entity, const Registry& registry) const override
        {
            return Inner.HasComponent(entity, registry);
        }
        bool Save(IWriteArchive& archive, EntityId entity, const World& world,
                  SceneSerializationContext& context) const override
        {
            ++Writes;
            return Inner.Save(archive, entity, world, context);
        }
        bool Load(IReadArchive& archive, EntityId entity, Registry& registry, SceneSerializationContext& context) override
        {
            return Inner.Load(archive, entity, registry, context);
        }
        bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world,
                           SceneSerializationContext& context) override
        {
            return Inner.LoadIntoWorld(archive, entity, world, context);
        }
        bool Remove(EntityId entity, Registry& registry) const override { return Inner.Remove(entity, registry); }
        ComponentPersistence Persistence() const override { return Inner.Persistence(); }
        bool SaveDefault(IWriteArchive& archive) const override { return Inner.SaveDefault(archive); }
        bool SavedFormFromRowAlone() const override { return Inner.SavedFormFromRowAlone(); }

    private:
        ComponentSerializer<FilterValues> Inner;
        int& Writes;
    };

    WorldComponentSchema SchemaWith(ComponentSerializerRegistry& serializers, int& writes)
    {
        WorldComponentSchema schema;
        ComponentRegistrar registrar(&schema, &serializers, nullptr);
        RegisterEngineComponents(registrar);
        schema.Add<FilterValues>();
        (void)serializers.Register(std::make_unique<CountingValues>(writes));
        schema.Seal();
        return schema;
    }

    class ZoneCaptureFilter : public ::testing::Test
    {
    protected:
        ZoneCaptureFilter()
            : Context(Logging)
            , Schema(SchemaWith(Serializers, Writes))
            , Runtime(Schema)
        {
            World& world = Entities();
            RegisterAbilityKit(world);
            GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
            Door = *tags.RegisterTag("filter.door");
            Opened = *tags.RegisterTag("filter.opened");
            Health = world.GetResource<AttributeRegistry>().RegisterAttribute("Health");
            world.SetResource(ZoneStateSerialization{ .Serializers = &Serializers, .Context = &Context });
            Scope = Store().Scope(kLevel, kHall);
        }

        World& Entities() { return Runtime.Entities(); }
        ZoneStateStore& Store() { return Entities().GetResource<ZoneStateStore>(); }
        EntityId Resolve(std::uint64_t id)
        {
            return Entities().GetResource<PersistentEntityIndex>().TryResolve(PersistentEntityId{ id });
        }

        JsonValue Written(ComponentTypeId type, std::string_view authored)
        {
            World& world = Entities();
            IComponentSerializer* serializer = Serializers.FindByType(type);
            const EntityId scratch = world.CreateEntity();
            const JsonValue parsed = *JsonParse(authored);
            JsonReadArchive read(parsed);
            EXPECT_TRUE(serializer->LoadIntoWorld(read, scratch, world, Context));
            JsonWriteArchive write;
            EXPECT_TRUE(serializer->Save(write, scratch, world, Context));
            world.DestroyEntity(scratch);
            return write.TakeValue();
        }

        EntityBuildPackage Hall(std::size_t props, std::size_t actors)
        {
            const JsonValue body = Written(ResolveComponentTypeId<RigidBody>(), R"({"motion": "dynamic"})");
            const JsonValue tags = Written(ResolveComponentTypeId<GameplayTagContainer>(),
                                           R"({"tags": [{"name": "filter.door", "stacks": 1}]})");
            const JsonValue attributes =
                Written(ResolveComponentTypeId<AttributeSet>(), R"({"attributes": [{"name": "Health", "base": 50}]})");
            const JsonValue responses = Written(ResolveComponentTypeId<Responses>(),
                                                R"({"bindings": {"open": {"on": "filter.used", "once": true}}})");
            const JsonValue values = Written(ResolveComponentTypeId<FilterValues>(),
                                             R"({"speed": 2, "steps": 1, "home": ")"
                                                 + PersistentEntityIdToString(PersistentEntityId{ 0x77 }) + R"("})");
            EntityBuildPackage package;
            for (std::size_t i = 0; i < props + actors; ++i)
            {
                const PackageEntityId entity = package.CreateEntity();
                const PersistentEntityId id{ kFirst + i };
                EXPECT_TRUE(package.AddComponent<PersistentIdComponent>(entity, { id }));
                EXPECT_TRUE(package.SetPersistentId(entity, id));
                const auto set = [&](ComponentTypeId type, const JsonValue& record) {
                    EXPECT_TRUE(package.SetSerializedComponent(entity, type, record));
                };
                if (i < props)
                {
                    set(ResolveComponentTypeId<LocalTransform>(),
                        *JsonParse(R"({"local": {"position": [)" + std::to_string(i) + R"(, 0.5, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}})"));
                    set(ResolveComponentTypeId<Collider>(), *JsonParse(R"({"shape": {"type": "box", "half_extents": [0.5, 0.5, 0.5]}})"));
                    set(ResolveComponentTypeId<RigidBody>(), body);
                }
                else
                {
                    set(ResolveComponentTypeId<GameplayTagContainer>(), tags);
                    set(ResolveComponentTypeId<AttributeSet>(), attributes);
                    set(ResolveComponentTypeId<Responses>(), responses);
                    set(ResolveComponentTypeId<FilterValues>(), values);
                }
            }
            return package;
        }

        void Finish()
        {
            Runtime.FlushLifecycleRequests();
            (void)Runtime.BeginResidencyProcessing();
            Runtime.FinalizeResidencyProcessing();
        }

        void Import(const EntityBuildPackage& package)
        {
            ZoneImportError error;
            ASSERT_TRUE(ImportZonePackage(Runtime, Schema, kHall, package, Serializers, Context, kLogic, &error, Scope))
                << error.Message;
            Finish();
        }

        void Detach()
        {
            ASSERT_TRUE(Runtime.RequestDetach(kHall));
            Finish();
        }

        std::vector<EntityId> Identified()
        {
            return IdentifiedEntitiesIn(Entities(), Runtime.FindZone(kHall)->Partition);
        }

        void Capture() { CaptureZoneState(Entities(), Scope, Identified(), ZoneCapture::Live); }

        std::string Unfiltered(EntityId entity)
        {
            const PersistentEntityId id = Entities().TryGet<PersistentIdComponent>(entity)->Id;
            const std::vector<AuthoredComponent>* baseline = Store().BaselineOf(Scope, id);
            if (baseline == nullptr)
                return "{}";
            return JsonStringify(CaptureEntityPatch(Entities(), entity, *baseline, ComponentPersistenceSet{ .Saved = true },
                                                    Serializers, Context, Store().PatchOf(Scope, id)));
        }

        std::string Kept(EntityId entity)
        {
            const JsonValue* patch = Store().PatchOf(Scope, Entities().TryGet<PersistentIdComponent>(entity)->Id);
            return patch != nullptr ? JsonStringify(*patch) : "{}";
        }

        int Writes = 0;
        LoggingProvider Logging;
        SceneSerializationContext Context;
        ComponentSerializerRegistry Serializers;
        WorldComponentSchema Schema;
        RuntimeWorld Runtime;
        GameplayTagId Door;
        GameplayTagId Opened;
        AttributeId Health;
        ZoneStateScope Scope;
    };

    template <typename T>
    bool FollowsRow()
    {
        return ComponentSerializer<T>().SavedFormFromRowAlone();
    }
}

TEST_F(ZoneCaptureFilter, AGeneratedFormFollowsItsRowOnlyWithoutAnAssetANamedIdOrAnEntityField)
{
    EXPECT_TRUE(FollowsRow<FilterValues>());
    EXPECT_FALSE(FollowsRow<FilterAsset>());
    EXPECT_FALSE(FollowsRow<FilterEntity>());
    EXPECT_FALSE(FollowsRow<FilterNamed>());

    const auto follows = [&](ComponentTypeId type) { return Serializers.FindByType(type)->SavedFormFromRowAlone(); };
    EXPECT_TRUE(follows(ResolveComponentTypeId<LocalTransform>()));
    EXPECT_TRUE(follows(ResolveComponentTypeId<RigidBody>()));
    EXPECT_TRUE(follows(ResolveComponentTypeId<GameplayTagContainer>()));
    EXPECT_TRUE(follows(ResolveComponentTypeId<AttributeSet>()));
    EXPECT_TRUE(follows(ResolveComponentTypeId<Responses>()));
    EXPECT_TRUE(follows(ResolveComponentTypeId<ItemPickup>()));
    EXPECT_TRUE(follows(ResolveComponentTypeId<ItemEntry>()));
    EXPECT_TRUE(follows(ResolveComponentTypeId<ItemContainer>()));
    EXPECT_FALSE(follows(ResolveComponentTypeId<AbilityGrants>()));
    EXPECT_FALSE(follows(ResolveComponentTypeId<EquipmentState>()));
}

TEST_F(ZoneCaptureFilter, APatchIsTheSameWithTheFilterAsWithoutItOverARandomisedRun)
{
    constexpr std::size_t kProps = 40;
    constexpr std::size_t kActors = 40;
    const EntityBuildPackage hall = Hall(kProps, kActors);
    Import(hall);
    std::mt19937 random(20261124u);
    const auto pick = [&](std::size_t from, std::size_t count) {
        return kFirst + from + std::uniform_int_distribution<std::size_t>(0, count - 1)(random);
    };
    std::size_t compared = 0;
    for (int step = 0; step < 120; ++step)
    {
        World& world = Entities();
        for (int change = 0; change < 6; ++change)
        {
            const int kind = std::uniform_int_distribution<int>(0, 9)(random);
            const EntityId prop = Resolve(pick(0, kProps));
            const EntityId actor = Resolve(pick(kProps, kActors));
            if (kind == 0 && world.IsAlive(prop))
                world.TryGet<LocalTransform>(prop)->Value.Position.X += 0.5f;
            else if (kind == 1 && world.IsAlive(prop))
                world.TryGet<LocalTransform>(prop)->Value.Position.X = static_cast<float>(pick(0, 4) - kFirst);
            else if (kind == 2 && world.IsAlive(prop) && world.HasComponent<RigidBody>(prop))
                world.TryGet<RigidBody>(prop)->Asleep = !world.TryGet<RigidBody>(prop)->Asleep;
            else if (kind == 3 && world.IsAlive(prop))
            {
                if (world.HasComponent<RigidBody>(prop))
                    world.RemoveComponent<RigidBody>(prop);
                else
                    world.AddComponent<RigidBody>(prop, RigidBody{});
            }
            else if (kind == 4 && world.IsAlive(prop) && !world.HasComponent<GameplayTagContainer>(prop))
                world.AddComponent<GameplayTagContainer>(prop);
            else if (kind == 5 && world.IsAlive(actor))
                (void)GrantBaselineTag(world, actor, Opened);
            else if (kind == 6 && world.IsAlive(actor))
                (void)RevokeBaselineTag(world, actor, Opened, 1);
            else if (kind == 7 && world.IsAlive(actor))
                world.TryGet<Responses>(actor)->Consumed ^= 1u;
            else if (kind == 8 && world.IsAlive(actor))
                world.TryGet<FilterValues>(actor)->Steps = static_cast<int>(pick(0, 3) - kFirst);
            else if (kind == 9 && world.IsAlive(actor))
                world.TryGet<AttributeSet>(actor)->Current[0] += 1.0f;
        }
        if (step % 40 == 39)
        {
            Detach();
            Import(hall);
        }
        Capture();
        for (const EntityId entity : Identified())
        {
            ASSERT_EQ(Kept(entity), Unfiltered(entity)) << "step " << step;
            ++compared;
        }
    }
    EXPECT_EQ(compared, 120u * (kProps + kActors));
}

TEST_F(ZoneCaptureFilter, AnUnchangedMemberIsNotWrittenAgainAndADetachAfterImportWritesWhatChanged)
{
    Import(Hall(0, 20));
    const int afterImport = Writes;
    Capture();
    EXPECT_EQ(Writes, afterImport) << "an untouched zone writes nothing";

    Entities().TryGet<FilterValues>(Resolve(kFirst + 3))->Steps = 9;
    Capture();
    EXPECT_EQ(Writes, afterImport + 1) << "only the changed row is written";

    World& world = Entities();
    std::vector<PersistentEntityId> gained;
    for (int i = 0; i < 5; ++i)
    {
        const EntityId dropped = world.CreateEntity(Runtime.FindZone(kHall)->Partition);
        world.AddComponent<LocalTransform>(dropped, LocalTransform{});
        world.AddComponent<FilterValues>(dropped, FilterValues{ .Speed = 3.0f, .Steps = i, .Home = {} });
        ASSERT_TRUE(KeepWithZone(world, dropped));
        gained.push_back(world.TryGet<PersistentIdComponent>(dropped)->Id);
    }
    Capture();
    const int afterGained = Writes;
    EXPECT_EQ(afterGained, afterImport + 1 + 5) << "a new gained member is written once";
    Capture();
    EXPECT_EQ(Writes, afterGained) << "an unchanged gained member is not written again";

    const EntityId moved = world.GetResource<PersistentEntityIndex>().TryResolve(gained[2]);
    world.TryGet<LocalTransform>(moved)->Value.Position.Y = 4.0f;
    Capture();
    EXPECT_EQ(Writes, afterGained + 1) << "a moved gained member is written again";

    Detach();
    const int beforeImport = Writes;
    Import(Hall(0, 20));
    ASSERT_TRUE(Runtime.FindZone(kHall) != nullptr);
    Entities().TryGet<FilterValues>(Resolve(kFirst + 7))->Steps = 4;
    const int imported = Writes;
    CaptureZoneState(Entities(), Scope, Identified(), ZoneCapture::Detach);
    EXPECT_EQ(Writes - imported, 2) << "the patched entity and the changed one; not the untouched or the gained";
    EXPECT_GE(imported, beforeImport);
}
