#pragma once

#include <core/logging/ILogSink.h>
#include <debug/DebugLogEntry.h>
#include <cstddef>
#include <string_view>
#include <vector>

//=============================================================================
// DebugLogSink
//
// An ILogSink that captures log messages into a fixed-capacity ring buffer.
// When the buffer is full the oldest entry is overwritten. Thread safety is
// intentionally omitted — all engine logging happens on the main thread.
//
// The captured entries are read by ConsolePanel (and any future panel that
// wants to display or filter log output).
//=============================================================================
class DebugLogSink : public ILogSink
{
public:
	static constexpr std::size_t DefaultCapacity = 512;

	explicit DebugLogSink(std::size_t capacity = DefaultCapacity);

	void Write(LogLevel level, std::string_view category, std::string_view message) override;

	// Read one entry in chronological order (oldest to newest).
	const DebugLogEntry& GetEntry(std::size_t chronologicalIndex) const;

	// Number of entries currently stored (capped at capacity).
	std::size_t Count() const { return Entries.size(); }

	// Index of the oldest entry in the ring (wraps after capacity is reached).
	std::size_t OldestIndex() const { return Head; }

	// Remove all stored entries and reset the ring.
	void Clear();

private:
	std::vector<DebugLogEntry> Entries;
	std::size_t Capacity;
	std::size_t Head = 0; // points to the next write position once full
	bool        Full = false;
};
