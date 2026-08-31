// Lifecycle hooks belong to the registration, not to the calling translation
// unit. Every structural route -- typed add and remove, the command buffer,
// initialization into a prebuilt row, and the type-erased add the editor uses --
// dispatches what RegisterComponent captured, so a unit that cannot see
// ComponentTraits still gets the component's hooks.
//
// The blind half lives in LifecycleBlindUnit.cpp, which includes only the
// component's declaration.

#include "LifecycleBlindUnit.h"

#include <ecs/CommandBuffer.h>
#include <ecs/Ecs.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
int g_BlindAdds    = 0;
int g_BlindRemoves = 0;
std::vector<int> g_BlindAddedValues;
} // namespace

// Visible here and nowhere else. This unit registers the component, so these are
// the hooks the World captures.
template <>
struct ComponentTraits<BlindHooked>
{
    static void OnAdd(BlindHooked& component, World&, EntityId)
    {
        ++g_BlindAdds;
        g_BlindAddedValues.push_back(component.Value);
    }

    static void OnRemove(const BlindHooked&, World&, EntityId)
    {
        ++g_BlindRemoves;
    }
};

namespace
{

class RegisteredLifecycleTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        g_BlindAdds = 0;
        g_BlindRemoves = 0;
        g_BlindAddedValues.clear();
        world.RegisterComponent<BlindHooked>();
    }

    World world;
};

} // namespace

TEST_F(RegisteredLifecycleTest, TypedAddFromABlindUnitFiresTheRegisteredHook)
{
    const EntityId entity = world.CreateEntity();
    AddBlindHookedFromBlindUnit(world, entity, 7);

    EXPECT_EQ(g_BlindAdds, 1);
    ASSERT_EQ(g_BlindAddedValues.size(), 1u);
    // The hook sees the component's stored value, not a default-constructed one.
    EXPECT_EQ(g_BlindAddedValues[0], 7);
}

TEST_F(RegisteredLifecycleTest, TypedRemoveFromABlindUnitFiresTheRegisteredHook)
{
    const EntityId entity = world.CreateEntity();
    AddBlindHookedFromBlindUnit(world, entity, 3);
    RemoveBlindHookedFromBlindUnit(world, entity);

    EXPECT_EQ(g_BlindRemoves, 1);
}

TEST_F(RegisteredLifecycleTest, CommandBufferAddFromABlindUnitFiresTheRegisteredHook)
{
    const EntityId entity = world.CreateEntity();
    AddBlindHookedViaCommandBufferFromBlindUnit(world, entity, 11);

    EXPECT_EQ(g_BlindAdds, 1);
    ASSERT_EQ(g_BlindAddedValues.size(), 1u);
    EXPECT_EQ(g_BlindAddedValues[0], 11);
}

TEST_F(RegisteredLifecycleTest, CommandBufferRemoveFromABlindUnitFiresTheRegisteredHook)
{
    const EntityId entity = world.CreateEntity();
    AddBlindHookedFromBlindUnit(world, entity, 5);
    RemoveBlindHookedViaCommandBufferFromBlindUnit(world, entity);

    EXPECT_EQ(g_BlindRemoves, 1);
}

// A run of adds for a hooked component must not be coalesced into the batch
// path, which fires no hooks at all. Recorded from the blind unit, because that
// is the case that can go wrong: whether a run may be batched is a fact about
// the component, and the recording unit is exactly the party that cannot see it.
TEST_F(RegisteredLifecycleTest, ABufferedRunOfHookedAddsFiresEveryHook)
{
    std::vector<EntityId> entities;
    for (int i = 0; i < 4; ++i)
        entities.push_back(world.CreateEntity());

    AddBlindHookedRunViaCommandBufferFromBlindUnit(world, entities);

    EXPECT_EQ(g_BlindAdds, 4);
    EXPECT_EQ(g_BlindAddedValues, (std::vector<int>{ 0, 1, 2, 3 }));
}

// The matching removal run: batching a run of removes would skip every
// OnRemove, which for a retain/release component is a leak.
TEST_F(RegisteredLifecycleTest, ABufferedRunOfHookedRemovesFiresEveryHook)
{
    std::vector<EntityId> entities;
    for (int i = 0; i < 4; ++i)
    {
        entities.push_back(world.CreateEntity());
        AddBlindHookedFromBlindUnit(world, entities.back(), i);
    }

    RemoveBlindHookedRunViaCommandBufferFromBlindUnit(world, entities);

    EXPECT_EQ(g_BlindRemoves, 4);
}

TEST_F(RegisteredLifecycleTest, InitializeFromABlindUnitFiresTheRegisteredHook)
{
    ArchetypeSignature signature;
    signature.set(world.GetComponentId<BlindHooked>());
    const EntityId entity = world.CreateEntityWithSignature(signature);

    InitializeBlindHookedFromBlindUnit(world, entity, 9);

    EXPECT_EQ(g_BlindAdds, 1);
    ASSERT_EQ(g_BlindAddedValues.size(), 1u);
    EXPECT_EQ(g_BlindAddedValues[0], 9);
}

// The route the editor's add-component menu takes: bytes and an id, with no type
// in hand at all. Its undo already removed through the registered hook; the add
// now retains through the matching one, so the pair balances.
TEST_F(RegisteredLifecycleTest, TheTypeErasedAddFiresTheRegisteredHook)
{
    const EntityId entity = world.CreateEntity();
    const BlindHooked value{ 21 };
    const ComponentMeta* meta = world.GetMeta(world.GetComponentId<BlindHooked>());
    ASSERT_NE(meta, nullptr);

    world.AddComponentRaw(entity, meta->Id, &value, meta->Size, meta->Alignment);

    EXPECT_EQ(g_BlindAdds, 1);
    ASSERT_EQ(g_BlindAddedValues.size(), 1u);
    EXPECT_EQ(g_BlindAddedValues[0], 21);

    world.RemoveComponentRaw(entity, meta->Id);
    EXPECT_EQ(g_BlindRemoves, 1);
}

// Registering the same component twice with different hook visibility is
// asserted in World::RegisterComponent, but it has no test here on purpose.
// Constructing it needs two registrations of one component that disagree about
// ComponentTraits, and within a single binary those are one implicitly
// instantiated template that the linker folds: whichever definition survives
// decides for the whole program, so the contradiction cannot be staged -- it
// only silently disarms the tests above. The case the assert guards is a game
// module carrying its own instantiation, which is a module-boundary test.
