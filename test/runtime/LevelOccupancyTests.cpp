#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/Serialize.h>
#include <ecs/WorldComponentSchema.h>
#include <physics/SensorOverlaps.h>
#include <physics/components/Collider.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZoneStateLists.h>
#include <zone/ZoneStateMemory.h>
#include <zone/ZoneStateStore.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr ZoneParticipation kPlayed{ .Visible = false, .Physics = true, .Logic = true, .Audio = false };
    constexpr ZoneId kYard{ 0x81 };
    const LevelReference kLevel{ .Kind = LevelKind::World, .Name = "yard" };
    constexpr PersistentEntityId kPlate{ 0x300 };
    constexpr PersistentEntityId kCrate{ 0x301 };

    WorldComponentSchema EngineSchema(ComponentSerializerRegistry& serializers)
    {
        WorldComponentSchema schema;
        ComponentRegistrar registrar(&schema, &serializers, nullptr);
        RegisterEngineComponents(registrar);
        schema.Seal();
        return schema;
    }

    std::vector<std::byte> PairBytes(PersistentEntityId sensor, PersistentEntityId occupant)
    {
        std::vector<std::byte> bytes(20);
        const std::uint32_t count = 1;
        std::memcpy(bytes.data(), &count, 4);
        std::memcpy(bytes.data() + 4, &sensor.Value, 8);
        std::memcpy(bytes.data() + 12, &occupant.Value, 8);
        return bytes;
    }

    class LevelOccupancy : public ::testing::Test
    {
    protected:
        LevelOccupancy()
            : Context(Logging)
            , Schema(EngineSchema(Serializers))
            , Runtime(Schema)
        {
            Entities().SetResource(ZoneStateSerialization{ .Serializers = &Serializers, .Context = &Context });
        }

        World& Entities() { return Runtime.Entities(); }
        ZoneStateStore& Store() { return Entities().GetResource<ZoneStateStore>(); }
        ZoneStateScope Scope() { return Store().Scope(kLevel, kYard); }

        static EntityBuildPackage Yard()
        {
            EntityBuildPackage package;
            const PackageEntityId plate = package.CreateEntity();
            EXPECT_TRUE(package.AddComponent<PersistentIdComponent>(plate, { kPlate }));
            EXPECT_TRUE(package.SetPersistentId(plate, kPlate));
            EXPECT_TRUE(package.SetSerializedComponent(
                plate, ResolveComponentTypeId<LocalTransform>(),
                *JsonParse(R"({"local": {"position": [0, 0.1, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}})")));
            EXPECT_TRUE(package.SetSerializedComponent(
                plate, ResolveComponentTypeId<Collider>(),
                *JsonParse(R"({"shape": {"type": "box", "half_extents": [0.6, 0.1, 0.6]}, "is_trigger": true})")));
            return package;
        }

        void FinishResidency()
        {
            Runtime.FlushLifecycleRequests();
            (void)Runtime.BeginResidencyProcessing();
            Runtime.FinalizeResidencyProcessing();
        }

        void Import(const EntityBuildPackage& package)
        {
            ZoneImportError error;
            ASSERT_TRUE(ImportZonePackage(Runtime, Schema, kYard, package, Serializers, Context, kPlayed, &error, Scope()))
                << error.Message;
            FinishResidency();
        }

        void Detach()
        {
            ASSERT_TRUE(Runtime.RequestDetach(kYard));
            FinishResidency();
        }

        std::optional<ZoneStateList> ExportedList(std::string_view name)
        {
            for (const DurableLevelState& level : Store().ExportDurable())
                for (const DurableZoneState& zone : level.Zones)
                    if (zone.Zone == kYard && zone.Lists != nullptr)
                        for (ZoneStateList& list : DecodeZoneLists(*zone.Lists).value_or(std::vector<ZoneStateList>{}))
                            if (list.Name == name)
                                return list;
            return std::nullopt;
        }

        LoggingProvider Logging;
        SceneSerializationContext Context;
        ComponentSerializerRegistry Serializers;
        WorldComponentSchema Schema;
        RuntimeWorld Runtime;
    };
}

TEST_F(LevelOccupancy, AnOccupiedListIsKeptAsReadByAZoneThatDoesNotImport)
{
    const ZoneStateList occupied{ std::string(kOccupiedZoneList), kZoneListVersion, PairBytes(kPlate, kCrate) };
    const std::vector<ZoneStateList> lists{ occupied };
    Store().Install(std::vector<DurableLevelState>{
        DurableLevelState{ kLevel, { DurableZoneState{ kYard, EncodeZoneLists(lists), nullptr } } } });
    const std::optional<ZoneStateList> kept = ExportedList(kOccupiedZoneList);
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ(*kept, occupied);
}

TEST_F(LevelOccupancy, AnOccupiedListOfANewerVersionIsCarriedThroughItsZonesRoundTrip)
{
    const ZoneStateList newer{ std::string(kOccupiedZoneList), kZoneListVersion + 1, PairBytes(kPlate, kCrate) };
    const std::vector<ZoneStateList> lists{ newer };
    Store().Install(std::vector<DurableLevelState>{
        DurableLevelState{ kLevel, { DurableZoneState{ kYard, EncodeZoneLists(lists), nullptr } } } });
    Import(Yard());
    Detach();
    const std::optional<ZoneStateList> carried = ExportedList(kOccupiedZoneList);
    ASSERT_TRUE(carried.has_value()) << "the zone lost a list it does not read";
    EXPECT_EQ(*carried, newer);
}

TEST_F(LevelOccupancy, AnOccupiedPairIsWrittenWithItsZonesCaptureAndAwaitedWhenTheZoneComesBack)
{
    Import(Yard());
    const EntityId plate = Entities().GetResource<PersistentEntityIndex>().TryResolve(kPlate);
    const EntityId crate = Entities().CreateEntity();
    Entities().AddComponent<PersistentIdComponent>(crate, PersistentIdComponent{ kCrate });
    SensorOverlaps& overlaps = Entities().AddResource<SensorOverlaps>();
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Observe(SensorPair{ plate, crate }, true));
    ASSERT_TRUE(overlaps.Commit(1));
    Detach();
    const std::optional<ZoneStateList> written = ExportedList(kOccupiedZoneList);
    ASSERT_TRUE(written.has_value()) << "the zone's capture did not write what its sensor held";
    EXPECT_EQ(DecodeOccupiedList(written->Payload), (std::vector<SensorSeed>{ SensorSeed{ kPlate, kCrate } }));

    Import(Yard());
    ASSERT_EQ(overlaps.Awaited().size(), 1u);
    EXPECT_EQ(overlaps.Awaited()[0], (SensorSeed{ kPlate, kCrate }));
}

TEST_F(LevelOccupancy, ASeedWhoseOccupantNeverArrivesIsDroppedAtTheNextCapture)
{
    const ZoneStateList occupied{ std::string(kOccupiedZoneList), kZoneListVersion, PairBytes(kPlate, kCrate) };
    const std::vector<ZoneStateList> lists{ occupied };
    Store().Install(std::vector<DurableLevelState>{
        DurableLevelState{ kLevel, { DurableZoneState{ kYard, EncodeZoneLists(lists), nullptr } } } });
    Import(Yard());
    ASSERT_NE(Entities().TryGetResource<SensorOverlaps>(), nullptr);
    ASSERT_EQ(Entities().GetResource<SensorOverlaps>().Awaited().size(), 1u);
    CaptureZoneState(Entities(), Scope(), IdentifiedEntitiesIn(Entities(), Runtime.FindZone(kYard)->Partition),
                     ZoneCapture::Live);
    EXPECT_TRUE(Entities().GetResource<SensorOverlaps>().Awaited().empty());
    EXPECT_TRUE(Store().OccupiedOf(Scope()).empty());
}
