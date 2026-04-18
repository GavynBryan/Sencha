#pragma once
#include <cstddef>
#include <span>
#include <type_traits>
#include <vector>

//=============================================================================
// EventBuffer<T>
//
// A typed, contiguous, append-only event buffer backed by std::vector<T>.
// This is a mechanical primitive â€” it knows nothing about event semantics,
// frame boundaries, subscribers, or dispatch. Domain-specific services
// own their own EventBuffer<TheirEventType> and decide when to Clear().
//
// Intended lifecycle (single-frame, clear-at-frame-start):
//   1. Clear() at the start of the frame
//   2. Producers Push/Emplace events during the frame
//   3. Consumers read via Items() later in the frame
//
// Safety:
//   - Readonly access is through std::span<const T>
//   - No exposed mutable container internals
//   - No raw void*, no type erasure, no virtual dispatch
//
// Performance:
//   - Contiguous storage for cache-friendly linear reads
//   - Reserve() to avoid reallocation churn
//   - No heap allocation per event (amortized vector growth)
//
// Emplace() returns a reference to the newly constructed element.
// WARNING: This reference is invalidated by any subsequent Push(),
// Emplace(), or Reserve() call that triggers reallocation. Do not
// store the returned reference across append operations.
//=============================================================================
template<typename T>
class EventBuffer
{
	static_assert(std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>,
		"EventBuffer<T> requires T to be at least move-constructible.");

public:
	// -- Capacity -------------------------------------------------------------

	void Reserve(std::size_t capacity)
	{
		Storage.reserve(capacity);
	}

	// -- Mutation --------------------------------------------------------------

	void Clear() noexcept
	{
		Storage.clear();
	}

	void Push(const T& value)
	{
		Storage.push_back(value);
	}

	void Push(T&& value)
	{
		Storage.push_back(std::move(value));
	}

	template<typename... Args>
	T& Emplace(Args&&... args)
	{
		return Storage.emplace_back(std::forward<Args>(args)...);
	}

	// -- Readonly access -------------------------------------------------------

	[[nodiscard]] std::span<const T> Items() const noexcept
	{
		return std::span<const T>(Storage);
	}

	[[nodiscard]] bool Empty() const noexcept
	{
		return Storage.empty();
	}

	[[nodiscard]] std::size_t Size() const noexcept
	{
		return Storage.size();
	}

	[[nodiscard]] std::size_t Capacity() const noexcept
	{
		return Storage.capacity();
	}

private:
	std::vector<T> Storage;
};
