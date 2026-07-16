#pragma once

#include <profiling/RenderStats.h>
#include <time/TimingHistory.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

//=============================================================================
// RenderCapture
//
// Bounded ring of per-frame {timing sample, render stats} records, armed by
// render.capture.start and drained by render.capture.write. Ring memory is
// allocated when recording first starts, never at engine startup. Records
// are plain structs buffered during the run; all string and JSON work
// happens only inside the explicit serialize calls. The serialized formats
// are the machine-analysis interface: schema-versioned envelope, stable
// keys, explicit units (_ms, _bytes, _count).
//=============================================================================
class RenderCapture
{
public:
	static constexpr std::size_t kDefaultCapacityFrames = 4096;
	static constexpr std::uint32_t kSchemaVersion = 1;

	struct FrameRecord
	{
		TimingFrameSample Timing;
		RenderStats Stats;
	};

	// Arms recording for up to `frameLimit` appended frames (0 = until the
	// ring wraps its capacity or Stop). Clears previously buffered records.
	void Start(std::size_t frameLimit);
	void Stop() { Recording = false; }
	[[nodiscard]] bool IsRecording() const { return Recording; }

	// Appends one finished frame while recording; disarms itself when the
	// frame limit is reached. Oldest records fall off past capacity.
	void Append(const TimingFrameSample& timing, const RenderStats& stats);

	[[nodiscard]] std::size_t Size() const { return Count; }
	// Advances only on Append; proof that no capture writes happen outside
	// Capture-mode recording.
	[[nodiscard]] std::uint64_t GetVersion() const { return Version; }

	// One metadata name/value pair for the envelope (the write command
	// snapshots the cvar registry through this shape).
	using MetadataPair = std::pair<std::string, std::string>;

	[[nodiscard]] std::string SerializeJson(
		const std::vector<MetadataPair>& cvarSnapshot) const;
	[[nodiscard]] std::string SerializeCsv() const;

private:
	[[nodiscard]] const FrameRecord& GetChronological(std::size_t index) const;

	std::vector<FrameRecord> Records;
	std::size_t Head = 0;
	std::size_t Count = 0;
	std::size_t RemainingFrames = 0;
	std::uint64_t Version = 0;
	bool Recording = false;
};
