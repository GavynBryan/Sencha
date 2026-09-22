// An archetype signature is one bit per dense component id, and a World may
// register up to MaxComponents of them. Hashing and matching a signature must
// hold for every bit, not only for the ids that fit in its first word.

#include <ecs/Ecs.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

template <int N>
struct WideComponent
{
    int Value = N;
};

namespace
{
    template <int N>
    constexpr std::array<char, 13> WideName()
    {
        std::array<char, 13> name{ 't', 'e', 's', 't', '.', 'w', 'i', 'd', 'e', '_', '0', '0', '0' };
        name[10] = static_cast<char>('0' + N / 100);
        name[11] = static_cast<char>('0' + (N / 10) % 10);
        name[12] = static_cast<char>('0' + N % 10);
        return name;
    }
}

template <int N>
struct ComponentTypeKey<WideComponent<N>>
{
    static constexpr std::array<char, 13> Storage = WideName<N>();
    static constexpr std::string_view Name{ Storage.data(), Storage.size() };
    static constexpr ComponentTypeId Id = MakeComponentTypeId(Name);
};

namespace
{
    constexpr int kWideCount = 70;

    template <int... N>
    void RegisterWide(World& world, std::integer_sequence<int, N...>)
    {
        (world.RegisterComponent<WideComponent<N>>(), ...);
    }
}

TEST(WideSignature, ComponentsPastTheFirstWordFormArchetypes)
{
    World world;
    RegisterWide(world, std::make_integer_sequence<int, kWideCount>{});

    const EntityId high = world.CreateEntity();
    world.AddComponent(high, WideComponent<kWideCount - 1>{});
    const EntityId mixed = world.CreateEntity();
    world.AddComponent(mixed, WideComponent<0>{});
    world.AddComponent(mixed, WideComponent<kWideCount - 1>{});
    world.AddComponent(mixed, WideComponent<64>{});

    ASSERT_NE(world.TryGet<WideComponent<kWideCount - 1>>(high), nullptr);
    EXPECT_EQ(world.TryGet<WideComponent<64>>(mixed)->Value, 64);

    int matched = 0;
    Query<Read<WideComponent<kWideCount - 1>>> query(world);
    query.ForEachChunk([&](auto& view) { matched += static_cast<int>(view.Count()); });
    EXPECT_EQ(matched, 2);

    world.RemoveComponent<WideComponent<64>>(mixed);
    EXPECT_EQ(world.TryGet<WideComponent<64>>(mixed), nullptr);
    EXPECT_NE(world.TryGet<WideComponent<0>>(mixed), nullptr);
}
