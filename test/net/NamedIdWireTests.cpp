#include <gtest/gtest.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/InventoryComponents.h>
#include <inventory/ItemDefinition.h>
#include <logic/Counter.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationWireContext.h>
#include <world/ComponentRegistrar.h>

#include <cstring>
#include <initializer_list>
#include <string_view>
#include <tuple>
#include <vector>

struct NamedWireProbe
{
    GameplayTagId Side;
    ItemId Carried;
    std::int32_t Standing = 0;
};

template <>
struct TypeSchema<NamedWireProbe>
{
    static constexpr std::string_view Name = "named_wire_probe";
    static constexpr bool Replicated = true;
    static auto Fields()
    {
        return std::tuple{ MakeField("side", &NamedWireProbe::Side).Optional(),
                           MakeField("carried", &NamedWireProbe::Carried).Optional(),
                           MakeField("standing", &NamedWireProbe::Standing) };
    }
};

namespace
{
    struct Machine
    {
        explicit Machine(std::initializer_list<std::string_view> tags, std::initializer_list<std::string_view> items)
        {
            GameplayTagRegistry& registry = Entities.AddResource<GameplayTagRegistry>();
            for (const std::string_view tag : tags)
                (void)registry.RegisterTag(tag);
            ItemRegistry& reserved = Entities.AddResource<ItemRegistry>();
            for (const std::string_view item : items)
                (void)reserved.Reserve(item);
        }

        GameplayTagId Tag(std::string_view name) { return Entities.GetResource<GameplayTagRegistry>().FindTag(name); }
        ItemId Item(std::string_view name) { return Entities.GetResource<ItemRegistry>().Find(name); }

        World Entities;
    };
}

TEST(NamedIdWire, TwoMachinesThatRegisteredNamesInDifferentOrdersExchangeAComponentByName)
{
    ReplicationLayout layout;
    ASSERT_TRUE(layout.Add<NamedWireProbe>()) << layout.ErrorDetail();
    layout.Seal();
    const ReplicatedComponent* component = layout.Find(ResolveComponentTypeId<NamedWireProbe>());
    ASSERT_NE(component, nullptr);
    ASSERT_NE(component->ToWire, nullptr) << "a named id travels as its name's wire key";
    EXPECT_EQ(component->Size, sizeof(NamedWireProbe));
    EXPECT_EQ(component->LocalSize, sizeof(NamedWireProbe));

    Machine sender({ "faction.red", "faction.blue" }, { "items/flare.sdata", "items/medkit.sdata" });
    Machine receiver({ "faction.blue", "faction.red" }, { "items/medkit.sdata", "items/flare.sdata" });
    ASSERT_NE(sender.Tag("faction.red"), receiver.Tag("faction.red"));
    ASSERT_NE(sender.Item("items/medkit.sdata"), receiver.Item("items/medkit.sdata"));

    const NamedWireProbe sent{ .Side = sender.Tag("faction.red"), .Carried = sender.Item("items/medkit.sdata"),
                               .Standing = -7 };
    ReplicationImage image(*component);
    ReplicationWireContext sending;
    sending.Entities = &sender.Entities;
    component->ToWire(sending,
                      std::as_bytes(std::span(&sent, 1)), image);

    NamedWireProbe arrived{};
    ReplicationWireContext receiving;
    receiving.Entities = &receiver.Entities;
    component->FromWire(receiving, image,
                        std::as_writable_bytes(std::span(&arrived, 1)));
    EXPECT_EQ(arrived.Side, receiver.Tag("faction.red"));
    EXPECT_EQ(arrived.Carried, receiver.Item("items/medkit.sdata"));
    EXPECT_EQ(arrived.Standing, -7);
}

TEST(NamedIdWire, ANamedLeafIsDescribedAsANameAndNoLongerReadOnly)
{
    const std::vector<RuntimeField>& fields = RuntimeFieldsOf<NamedWireProbe>();
    ASSERT_EQ(fields.size(), 3u);
    EXPECT_EQ(fields[0].Names, &NamedIdField<GameplayTagId>::Vocabulary());
    EXPECT_TRUE(fields[0].Optional);
    EXPECT_FALSE(fields[0].ReadOnly);
    EXPECT_EQ(fields[0].Scalar, FieldScalar::UInt32);
    EXPECT_EQ(fields[1].Names, &NamedIdField<ItemId>::Vocabulary());
    EXPECT_EQ(fields[2].Names, nullptr);
}

TEST(NamedIdWire, CountersAndEntriesKeepTheirWireLayout)
{
    ReplicationLayout layout;
    ComponentRegistrar registrar(nullptr, nullptr, &layout);
    registrar.Add<Counter>();
    registrar.Add<ItemEntry>();
    layout.Seal();
    ASSERT_EQ(layout.Error(), ReplicationLayoutError::None) << layout.ErrorDetail();
    for (const ComponentTypeId type : { ResolveComponentTypeId<Counter>(), ResolveComponentTypeId<ItemEntry>() })
    {
        const ReplicatedComponent* component = layout.Find(type);
        ASSERT_NE(component, nullptr);
        EXPECT_NE(component->ToWire, nullptr);
        EXPECT_EQ(component->Size, 8u);
        EXPECT_EQ(component->LocalSize, 8u);
    }
    EXPECT_EQ(layout.TableHash(), 0x82EDB21EC2D02633ULL);
}
