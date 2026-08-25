// Probe-volume slot residency policy, exercised without an image service or a
// descriptor set.
//
// ProbeVolumeSet itself refuses to do anything with a null VulkanImageService,
// so every branch below was unreachable from a headless test while the slot
// table lived inside it as a bare bool array. The three-state machine is the
// testable half of the retirement gate; what stays untestable here is
// ProbeVolumeSet's queue of retiring volumes, which needs a live frame clock
// and real images.

#include <gtest/gtest.h>

#include <render/ProbeVolumeSlotTable.h>

TEST(ProbeVolumeSlotTable, StartsEmpty)
{
    const ProbeVolumeSlotTable slots;
    EXPECT_EQ(slots.UsedCount(), 0u);
    EXPECT_FALSE(slots.IsFull());
    for (std::uint32_t slot = 0; slot < ProbeVolumeSlotTable::kCapacity; ++slot)
        EXPECT_FALSE(slots.IsUsed(slot));
}

TEST(ProbeVolumeSlotTable, AcquiresTheLowestFreeSlotInOrder)
{
    ProbeVolumeSlotTable slots;
    for (std::uint32_t expected = 0;
         expected < ProbeVolumeSlotTable::kCapacity;
         ++expected)
    {
        EXPECT_EQ(slots.Acquire(), expected);
    }
    EXPECT_TRUE(slots.IsFull());
}

TEST(ProbeVolumeSlotTable, AcquireMarksTheSlotUsedImmediately)
{
    ProbeVolumeSlotTable slots;
    const std::uint32_t slot = slots.Acquire();
    ASSERT_NE(slot, ProbeVolumeSlotTable::kInvalidSlot);
    EXPECT_TRUE(slots.IsUsed(slot));
    EXPECT_EQ(slots.UsedCount(), 1u);
    // The second acquire must not hand back the same index: ProbeVolumeSet
    // acquires before it uploads, so an unmarked slot would be handed to two
    // volumes and the second would overwrite the first's binding.
    EXPECT_NE(slots.Acquire(), slot);
}

TEST(ProbeVolumeSlotTable, DeniesWhenFullRatherThanEvicting)
{
    ProbeVolumeSlotTable slots;
    for (std::uint32_t i = 0; i < ProbeVolumeSlotTable::kCapacity; ++i)
        (void)slots.Acquire();

    EXPECT_EQ(slots.Acquire(), ProbeVolumeSlotTable::kInvalidSlot);
    // Denial leaves the resident set untouched; the caller falls back to
    // hemispheric ambient for the volume it could not place.
    EXPECT_EQ(slots.UsedCount(), ProbeVolumeSlotTable::kCapacity);
    for (std::uint32_t slot = 0; slot < ProbeVolumeSlotTable::kCapacity; ++slot)
        EXPECT_TRUE(slots.IsUsed(slot));
}

// This test previously asserted that a released slot was handed straight back
// to the next Acquire. That was the defect stated as a contract: the frame that
// sampled the slot may still be executing, and its uniform header names the
// slot by index, so reuse before retirement makes that frame read the incoming
// volume through the outgoing volume's mapping.
TEST(ProbeVolumeSlotTable, RetiringSlotIsNotAcquirableUntilItIsReclaimed)
{
    ProbeVolumeSlotTable slots;
    for (std::uint32_t i = 0; i < ProbeVolumeSlotTable::kCapacity; ++i)
        (void)slots.Acquire();

    constexpr std::uint32_t freed = 3;
    slots.BeginRetire(freed);
    EXPECT_EQ(slots.StateOf(freed), ProbeVolumeSlotTable::State::Retiring);
    // Still occupied: denial is correct here, and the caller falls back to
    // hemispheric ambient for the frames the predecessor takes to retire.
    EXPECT_TRUE(slots.IsUsed(freed));
    EXPECT_TRUE(slots.IsFull());
    EXPECT_EQ(slots.Acquire(), ProbeVolumeSlotTable::kInvalidSlot);

    slots.Release(freed);
    EXPECT_EQ(slots.StateOf(freed), ProbeVolumeSlotTable::State::Free);
    EXPECT_EQ(slots.Acquire(), freed);
}

TEST(ProbeVolumeSlotTable, BeginRetireOnlyMovesAResidentSlot)
{
    ProbeVolumeSlotTable slots;
    const std::uint32_t slot = slots.Acquire();
    ASSERT_EQ(slot, 0u);

    // A free slot cannot retire: releasing a zone twice must not drag whoever
    // acquired the slot in between into a retirement it never asked for.
    slots.Release(slot);
    slots.BeginRetire(slot);
    EXPECT_EQ(slots.StateOf(slot), ProbeVolumeSlotTable::State::Free);

    const std::uint32_t reacquired = slots.Acquire();
    ASSERT_EQ(reacquired, slot);
    slots.BeginRetire(reacquired);
    slots.BeginRetire(reacquired);
    EXPECT_EQ(slots.StateOf(reacquired), ProbeVolumeSlotTable::State::Retiring);
    EXPECT_EQ(slots.CountIn(ProbeVolumeSlotTable::State::Retiring), 1u);

    slots.BeginRetire(ProbeVolumeSlotTable::kInvalidSlot);
    slots.BeginRetire(ProbeVolumeSlotTable::kCapacity + 17);
    EXPECT_EQ(slots.CountIn(ProbeVolumeSlotTable::State::Retiring), 1u);
}

TEST(ProbeVolumeSlotTable, CountsSeparateResidentFromRetiring)
{
    ProbeVolumeSlotTable slots;
    for (std::uint32_t i = 0; i < ProbeVolumeSlotTable::kCapacity; ++i)
        (void)slots.Acquire();
    EXPECT_EQ(slots.CountIn(ProbeVolumeSlotTable::State::Resident),
              ProbeVolumeSlotTable::kCapacity);

    slots.BeginRetire(2);
    slots.BeginRetire(6);
    EXPECT_EQ(slots.CountIn(ProbeVolumeSlotTable::State::Resident),
              ProbeVolumeSlotTable::kCapacity - 2);
    EXPECT_EQ(slots.CountIn(ProbeVolumeSlotTable::State::Retiring), 2u);
    EXPECT_EQ(slots.CountIn(ProbeVolumeSlotTable::State::Free), 0u);
    // Both still count as used: that is what makes the set read as full while
    // the outgoing zone's frames drain.
    EXPECT_EQ(slots.UsedCount(), ProbeVolumeSlotTable::kCapacity);
}

TEST(ProbeVolumeSlotTable, ReleaseIsIdempotentAndIgnoresOutOfRange)
{
    ProbeVolumeSlotTable slots;
    const std::uint32_t slot = slots.Acquire();
    ASSERT_EQ(slot, 0u);

    slots.Release(slot);
    slots.Release(slot);
    EXPECT_EQ(slots.UsedCount(), 0u);

    // Teardown releases per volume and a zone can be released twice; neither
    // an out-of-range index nor the denial sentinel may corrupt the table.
    slots.Release(ProbeVolumeSlotTable::kInvalidSlot);
    slots.Release(ProbeVolumeSlotTable::kCapacity + 17);
    EXPECT_EQ(slots.UsedCount(), 0u);
    EXPECT_EQ(slots.Acquire(), 0u);
}

TEST(ProbeVolumeSlotTable, ReleaseAllFreesEveryResidentSlot)
{
    ProbeVolumeSlotTable slots;
    for (std::uint32_t i = 0; i < ProbeVolumeSlotTable::kCapacity; ++i)
        (void)slots.Acquire();
    slots.BeginRetire(4);

    slots.ReleaseAll();
    EXPECT_EQ(slots.UsedCount(), 0u);
    EXPECT_FALSE(slots.IsFull());
    EXPECT_EQ(slots.Acquire(), 0u);
}

TEST(ProbeVolumeSlotTable, IsUsedRejectsIndicesPastCapacity)
{
    ProbeVolumeSlotTable slots;
    for (std::uint32_t i = 0; i < ProbeVolumeSlotTable::kCapacity; ++i)
        (void)slots.Acquire();

    EXPECT_FALSE(slots.IsUsed(ProbeVolumeSlotTable::kCapacity));
    EXPECT_FALSE(slots.IsUsed(ProbeVolumeSlotTable::kInvalidSlot));
}

TEST(ProbeVolumeSlotTable, InterleavedRetireAndAcquireStaysDense)
{
    ProbeVolumeSlotTable slots;
    for (std::uint32_t i = 0; i < ProbeVolumeSlotTable::kCapacity; ++i)
        (void)slots.Acquire();

    // A streaming pattern: two zones unload out of order, their frames retire,
    // two zones load. Reclaim order is the release order, not the slot order,
    // but acquisition stays lowest-free-wins.
    slots.BeginRetire(5);
    slots.BeginRetire(1);
    EXPECT_TRUE(slots.IsFull());

    slots.Release(5);
    slots.Release(1);
    EXPECT_EQ(slots.UsedCount(), ProbeVolumeSlotTable::kCapacity - 2);

    EXPECT_EQ(slots.Acquire(), 1u);
    EXPECT_EQ(slots.Acquire(), 5u);
    EXPECT_TRUE(slots.IsFull());
    EXPECT_EQ(slots.Acquire(), ProbeVolumeSlotTable::kInvalidSlot);
}
