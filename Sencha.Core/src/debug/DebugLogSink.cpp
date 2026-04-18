#include <debug/DebugLogSink.h>
#include <cassert>

DebugLogSink::DebugLogSink(std::size_t capacity)
	: Capacity(capacity)
{
	Entries.reserve(capacity);
}

void DebugLogSink::Write(LogLevel level, std::string_view category, std::string_view message)
{
	if (level < MinLevel)
		return;
	if (Capacity == 0)
		return;

	DebugLogEntry entry{
		.Level    = level,
		.Category = std::string(category),
		.Message  = std::string(message),
	};

	if (Entries.size() < Capacity)
	{
		Entries.push_back(std::move(entry));
	}
	else
	{
		// Ring is full — overwrite oldest slot.
		Entries[Head] = std::move(entry);
		Head = (Head + 1) % Capacity;
		Full = true;
	}
}

const DebugLogEntry& DebugLogSink::GetEntry(std::size_t chronologicalIndex) const
{
	assert(chronologicalIndex < Entries.size());

	if (!Full)
		return Entries[chronologicalIndex];

	const std::size_t physicalIndex = (Head + chronologicalIndex) % Capacity;
	return Entries[physicalIndex];
}

void DebugLogSink::Clear()
{
	Entries.clear();
	Head = 0;
	Full = false;
}
