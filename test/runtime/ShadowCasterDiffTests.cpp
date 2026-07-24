#include <gtest/gtest.h>

#include <render/ShadowCasterSet.h>

#include <vector>

namespace
{
    RenderEntityKey MakeKey(std::uint32_t entityIndex, std::uint32_t zone = 1)
    {
        return RenderEntityKey{
            .Kind = RegistryKind::Zone,
            .Zone = ZoneId{ zone },
            .Entity = EntityId{ entityIndex, 1 },
        };
    }

    ShadowCasterRecord MakeRecord(const RenderEntityKey& key,
                                  const Aabb3d& bounds,
                                  std::uint32_t mask = 1,
                                  std::uint64_t materialHash = 7)
    {
        return ShadowCasterRecord{
            .Key = key,
            .State = ShadowCasterState{
                .WorldBounds = QuantizeShadowCasterBounds(bounds),
                .Mesh = StaticMeshHandle{ 4, 1 },
                .Materials = MaterialSetHandle{ 9, 1 },
                .EffectiveShadowSectionMask = mask,
                .ShadowMaterialStateHash = materialHash,
            },
        };
    }

    Aabb3d Box(float minX, float maxX)
    {
        return Aabb3d(Vec3d(minX, 0.0f, 0.0f), Vec3d(maxX, 1.0f, 1.0f));
    }
}

TEST(ShadowCasterDiff, FirstFrameEmitsAddedWithCurrentBounds)
{
    ShadowCasterDiff diff;
    std::vector<ShadowCasterRecord> current{ MakeRecord(MakeKey(1), Box(0.0f, 2.0f)) };
    std::vector<ShadowCasterEvent> events;
    diff.Apply(current, true, events);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].Change, ShadowCasterEvent::Kind::Added);
    EXPECT_FLOAT_EQ(events[0].Bounds.Max.X, 2.0f);
}

TEST(ShadowCasterDiff, UnchangedStateEmitsNothing)
{
    ShadowCasterDiff diff;
    std::vector<ShadowCasterEvent> events;
    for (int frame = 0; frame < 2; ++frame)
    {
        std::vector<ShadowCasterRecord> current{
            MakeRecord(MakeKey(1), Box(0.0f, 2.0f)),
            MakeRecord(MakeKey(2), Box(5.0f, 6.0f)),
        };
        events.clear();
        diff.Apply(current, true, events);
    }
    EXPECT_TRUE(events.empty());
}

TEST(ShadowCasterDiff, MovedCasterEmitsChangedUnionCoveringTheDepartureSite)
{
    ShadowCasterDiff diff;
    std::vector<ShadowCasterEvent> events;

    std::vector<ShadowCasterRecord> first{ MakeRecord(MakeKey(1), Box(0.0f, 2.0f)) };
    diff.Apply(first, true, events);
    events.clear();

    std::vector<ShadowCasterRecord> second{ MakeRecord(MakeKey(1), Box(10.0f, 12.0f)) };
    diff.Apply(second, true, events);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].Change, ShadowCasterEvent::Kind::Changed);
    // The union spans both sites, so a shadow volume at the departure
    // position still sees the event.
    EXPECT_FLOAT_EQ(events[0].Bounds.Min.X, 0.0f);
    EXPECT_FLOAT_EQ(events[0].Bounds.Max.X, 12.0f);
}

TEST(ShadowCasterDiff, RemovalCarriesThePreviousBounds)
{
    ShadowCasterDiff diff;
    std::vector<ShadowCasterEvent> events;

    std::vector<ShadowCasterRecord> first{
        MakeRecord(MakeKey(1), Box(0.0f, 2.0f)),
        MakeRecord(MakeKey(2, 2), Box(20.0f, 22.0f)),
    };
    diff.Apply(first, true, events);
    events.clear();

    // Zone 2 detached: its record simply stops appearing.
    std::vector<ShadowCasterRecord> second{ MakeRecord(MakeKey(1), Box(0.0f, 2.0f)) };
    diff.Apply(second, true, events);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].Change, ShadowCasterEvent::Kind::Removed);
    EXPECT_FLOAT_EQ(events[0].Bounds.Min.X, 20.0f);
    EXPECT_FLOAT_EQ(events[0].Bounds.Max.X, 22.0f);
}

TEST(ShadowCasterDiff, MaterialSetReplacementEmitsChanged)
{
    ShadowCasterDiff diff;
    std::vector<ShadowCasterEvent> events;

    std::vector<ShadowCasterRecord> first{ MakeRecord(MakeKey(1), Box(0.0f, 2.0f)) };
    diff.Apply(first, true, events);
    events.clear();

    std::vector<ShadowCasterRecord> second{ MakeRecord(MakeKey(1), Box(0.0f, 2.0f)) };
    second[0].State.Materials = MaterialSetHandle{ 11, 1 };
    diff.Apply(second, true, events);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].Change, ShadowCasterEvent::Kind::Changed);
}

TEST(ShadowCasterDiff, ShadowMaterialStateHashChangeEmitsChanged)
{
    ShadowCasterDiff diff;
    std::vector<ShadowCasterEvent> events;

    std::vector<ShadowCasterRecord> first{
        MakeRecord(MakeKey(1), Box(0.0f, 2.0f), 1, 7),
    };
    diff.Apply(first, true, events);
    events.clear();

    std::vector<ShadowCasterRecord> second{
        MakeRecord(MakeKey(1), Box(0.0f, 2.0f), 1, 8),
    };
    diff.Apply(second, true, events);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].Change, ShadowCasterEvent::Kind::Changed);
}

TEST(ShadowCasterDiff, QuantizationSwallowsTransformNoise)
{
    ShadowCasterDiff diff;
    std::vector<ShadowCasterEvent> events;

    std::vector<ShadowCasterRecord> first{ MakeRecord(MakeKey(1), Box(0.0f, 2.0f)) };
    diff.Apply(first, true, events);
    events.clear();

    std::vector<ShadowCasterRecord> second{
        MakeRecord(MakeKey(1), Box(0.0001f, 2.0001f)),
    };
    diff.Apply(second, true, events);
    EXPECT_TRUE(events.empty());
}

TEST(ShadowCasterDiff, SkippedEmissionStillSwapsTheTables)
{
    ShadowCasterDiff diff;
    std::vector<ShadowCasterEvent> events;

    std::vector<ShadowCasterRecord> first{ MakeRecord(MakeKey(1), Box(0.0f, 2.0f)) };
    diff.Apply(first, false, events);
    EXPECT_TRUE(events.empty());

    // The suppressed frame still became the previous table, so re-presenting
    // the same state is quiet and only a real change speaks.
    std::vector<ShadowCasterRecord> second{ MakeRecord(MakeKey(1), Box(0.0f, 2.0f)) };
    diff.Apply(second, true, events);
    EXPECT_TRUE(events.empty());

    std::vector<ShadowCasterRecord> third{ MakeRecord(MakeKey(1), Box(4.0f, 6.0f)) };
    diff.Apply(third, true, events);
    EXPECT_EQ(events.size(), 1u);
}

TEST(ShadowCasterDiff, UnsortedInputStillDiffsByKeyOrder)
{
    ShadowCasterDiff diff;
    std::vector<ShadowCasterEvent> events;

    std::vector<ShadowCasterRecord> first{
        MakeRecord(MakeKey(9), Box(0.0f, 1.0f)),
        MakeRecord(MakeKey(2), Box(2.0f, 3.0f)),
        MakeRecord(MakeKey(5), Box(4.0f, 5.0f)),
    };
    diff.Apply(first, true, events);
    events.clear();

    std::vector<ShadowCasterRecord> second{
        MakeRecord(MakeKey(5), Box(4.0f, 5.0f)),
        MakeRecord(MakeKey(9), Box(0.0f, 1.0f)),
        MakeRecord(MakeKey(2), Box(2.0f, 3.0f)),
    };
    diff.Apply(second, true, events);
    EXPECT_TRUE(events.empty());
}
