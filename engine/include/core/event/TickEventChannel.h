#pragma once
#include <core/event/EventBuffer.h>

#include <span>
#include <utility>
#include <vector>

//=============================================================================
// TickEventChannel<T>
//
// Tick-scoped publication over EventBuffer<T> for events that can be raised
// outside the step that consumers read them in. Two containers:
//
//   published — what consumers see for the current tick
//   pending   — staged by code running between ticks (registry residency
//               processing, teardown), folded into published at tick entry
//
// Lifecycle, driven by the owning system:
//   1. BeginTick() at step entry: clear last tick's published events, then
//      move everything staged since the last executed tick into published.
//   2. Publish() during the step appends directly to published.
//   3. Consumers read Items() later in the same tick (e.g. PostFixed).
//
// A frame that runs zero ticks leaves staged events pending; they surface in
// the next tick that actually executes. Frame-lane consumers see only the
// last executed tick's events. Single-threaded by contract: staging sites
// (drain-point residency processing, registry teardown) and publication share
// the simulation thread.
//
// The owner outlives every producer that holds a pointer to the channel —
// the same lifetime argument bindings already rely on for the shared
// simulation itself.
//=============================================================================
template <typename T>
class TickEventChannel
{
public:
	// Stage an event from outside the step. Not visible until the next
	// BeginTick folds it into the published buffer.
	void Stage(const T& event)
	{
		Pending.push_back(event);
	}

	void Stage(T&& event)
	{
		Pending.push_back(std::move(event));
	}

	// Publish an event during the step: visible to this tick's consumers.
	void Publish(const T& event)
	{
		Published.Push(event);
	}

	void Publish(T&& event)
	{
		Published.Push(std::move(event));
	}

	// Called once at step entry by the owning system. Clears the previous
	// tick's events, then promotes everything staged since.
	void BeginTick()
	{
		Published.Clear();
		for (T& event : Pending)
			Published.Push(std::move(event));
		Pending.clear();
	}

	[[nodiscard]] std::span<const T> Items() const noexcept
	{
		return Published.Items();
	}

	[[nodiscard]] bool Empty() const noexcept
	{
		return Published.Empty();
	}

	[[nodiscard]] std::size_t Size() const noexcept
	{
		return Published.Size();
	}

	[[nodiscard]] std::size_t PendingCount() const noexcept
	{
		return Pending.size();
	}

	void Reserve(std::size_t capacity)
	{
		Published.Reserve(capacity);
		Pending.reserve(capacity);
	}

private:
	EventBuffer<T> Published;
	std::vector<T> Pending;
};
