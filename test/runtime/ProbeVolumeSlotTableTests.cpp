// Probe-volume slot residency policy, exercised without an image service or a
// descriptor set.
//
// ProbeVolumeSet itself refuses to do anything with a null VulkanImageService,
// so every branch below was unreachable from a headless test while the slot
// table lived inside it as a bare bool array.

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

TEST(ProbeVolumeSlotTable, ReleasedSlotIsReusedByTheNextAcquire)
{
    ProbeVolumeSlotTable slots;
    for (std::uint32_t i = 0; i < ProbeVolumeSlotTable::kCapacity; ++i)
        (void)slots.Acquire();

    constexpr std::uint32_t freed = 3;
    slots.Release(freed);
    EXPECT_FALSE(slots.IsUsed(freed));
    EXPECT_EQ(slots.Acquire(), freed);
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

TEST(ProbeVolumeSlotTable, InterleavedReleaseAndAcquireStaysDense)
{
    ProbeVolumeSlotTable slots;
    for (std::uint32_t i = 0; i < ProbeVolumeSlotTable::kCapacity; ++i)
        (void)slots.Acquire();

    // A streaming pattern: two zones unload out of order, two load.
    slots.Release(5);
    slots.Release(1);
    EXPECT_EQ(slots.UsedCount(), ProbeVolumeSlotTable::kCapacity - 2);

    EXPECT_EQ(slots.Acquire(), 1u);
    EXPECT_EQ(slots.Acquire(), 5u);
    EXPECT_TRUE(slots.IsFull());
    EXPECT_EQ(slots.Acquire(), ProbeVolumeSlotTable::kInvalidSlot);
}
