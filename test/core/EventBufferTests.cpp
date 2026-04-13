#include <gtest/gtest.h>
#include <core/event/EventBuffer.h>

// --- Test helpers ---

struct TestEvent
{
	int Type = 0;
	float Value = 0.0f;

	TestEvent() = default;
	TestEvent(int type, float value) : Type(type), Value(value) {}
};

// --- EventBuffer Tests ---

TEST(EventBuffer, StartsEmpty)
{
	EventBuffer<TestEvent> buffer;

	EXPECT_TRUE(buffer.Empty());
	EXPECT_EQ(buffer.Size(), 0u);
	EXPECT_EQ(buffer.Items().size(), 0u);
}

TEST(EventBuffer, PushByConstRef)
{
	EventBuffer<TestEvent> buffer;
	TestEvent event{1, 42.0f};

	buffer.Push(event);

	EXPECT_FALSE(buffer.Empty());
	EXPECT_EQ(buffer.Size(), 1u);
	EXPECT_EQ(buffer.Items()[0].Type, 1);
	EXPECT_FLOAT_EQ(buffer.Items()[0].Value, 42.0f);
}

TEST(EventBuffer, PushByRvalue)
{
	EventBuffer<TestEvent> buffer;

	buffer.Push(TestEvent{2, 7.5f});

	EXPECT_EQ(buffer.Size(), 1u);
	EXPECT_EQ(buffer.Items()[0].Type, 2);
	EXPECT_FLOAT_EQ(buffer.Items()[0].Value, 7.5f);
}

TEST(EventBuffer, EmplaceConstructsInPlace)
{
	EventBuffer<TestEvent> buffer;

	TestEvent& ref = buffer.Emplace(3, 99.0f);

	EXPECT_EQ(buffer.Size(), 1u);
	EXPECT_EQ(ref.Type, 3);
	EXPECT_FLOAT_EQ(ref.Value, 99.0f);

	// The returned reference points into the buffer
	EXPECT_EQ(&ref, &buffer.Items()[0]);
}

TEST(EventBuffer, ClearRemovesAllEvents)
{
	EventBuffer<TestEvent> buffer;
	buffer.Push(TestEvent{1, 1.0f});
	buffer.Push(TestEvent{2, 2.0f});
	buffer.Push(TestEvent{3, 3.0f});

	buffer.Clear();

	EXPECT_TRUE(buffer.Empty());
	EXPECT_EQ(buffer.Size(), 0u);
	EXPECT_EQ(buffer.Items().size(), 0u);
}

TEST(EventBuffer, ClearDoesNotDeallocate)
{
	EventBuffer<TestEvent> buffer;
	buffer.Reserve(64);
	buffer.Push(TestEvent{1, 1.0f});

	std::size_t capBefore = buffer.Capacity();
	buffer.Clear();

	// vector::clear() preserves capacity
	EXPECT_EQ(buffer.Capacity(), capBefore);
}

TEST(EventBuffer, ReserveSetsMinimumCapacity)
{
	EventBuffer<TestEvent> buffer;

	buffer.Reserve(128);

	EXPECT_GE(buffer.Capacity(), 128u);
	EXPECT_TRUE(buffer.Empty());
}

TEST(EventBuffer, ItemsReturnsContiguousSpan)
{
	EventBuffer<TestEvent> buffer;
	buffer.Push(TestEvent{10, 1.0f});
	buffer.Push(TestEvent{20, 2.0f});
	buffer.Push(TestEvent{30, 3.0f});

	auto items = buffer.Items();

	ASSERT_EQ(items.size(), 3u);
	EXPECT_EQ(items[0].Type, 10);
	EXPECT_EQ(items[1].Type, 20);
	EXPECT_EQ(items[2].Type, 30);

	// Contiguity: addresses should be sequential
	EXPECT_EQ(&items[1], &items[0] + 1);
	EXPECT_EQ(&items[2], &items[0] + 2);
}

TEST(EventBuffer, ItemsSpanIsReadonly)
{
	EventBuffer<TestEvent> buffer;
	buffer.Push(TestEvent{1, 1.0f});

	auto items = buffer.Items();

	// std::span<const T> â€” this is a compile-time guarantee.
	// The line below should NOT compile if uncommented:
	// items[0].Type = 999;
	static_assert(std::is_const_v<std::remove_reference_t<decltype(items[0])>>,
		"Items() must return span<const T>");
}

TEST(EventBuffer, MultipleEventsPreserveOrder)
{
	EventBuffer<TestEvent> buffer;

	for (int i = 0; i < 100; ++i)
	{
		buffer.Push(TestEvent{i, static_cast<float>(i)});
	}

	auto items = buffer.Items();
	ASSERT_EQ(items.size(), 100u);

	for (int i = 0; i < 100; ++i)
	{
		EXPECT_EQ(items[i].Type, i);
	}
}

TEST(EventBuffer, ClearThenReuseWorks)
{
	EventBuffer<TestEvent> buffer;
	buffer.Push(TestEvent{1, 1.0f});
	buffer.Push(TestEvent{2, 2.0f});

	buffer.Clear();

	buffer.Push(TestEvent{10, 10.0f});

	EXPECT_EQ(buffer.Size(), 1u);
	EXPECT_EQ(buffer.Items()[0].Type, 10);
}

TEST(EventBuffer, CapacityGrowsWithPush)
{
	EventBuffer<TestEvent> buffer;
	EXPECT_EQ(buffer.Capacity(), 0u);

	buffer.Push(TestEvent{1, 1.0f});
	EXPECT_GE(buffer.Capacity(), 1u);
}

TEST(EventBuffer, WorksWithMoveOnlyType)
{
	struct MoveOnly
	{
		int Value = 0;
		MoveOnly() = default;
		explicit MoveOnly(int v) : Value(v) {}
		MoveOnly(const MoveOnly&) = delete;
		MoveOnly& operator=(const MoveOnly&) = delete;
		MoveOnly(MoveOnly&&) = default;
		MoveOnly& operator=(MoveOnly&&) = default;
	};

	EventBuffer<MoveOnly> buffer;
	buffer.Emplace(42);
	buffer.Push(MoveOnly{7});

	EXPECT_EQ(buffer.Size(), 2u);
	EXPECT_EQ(buffer.Items()[0].Value, 42);
	EXPECT_EQ(buffer.Items()[1].Value, 7);
}
