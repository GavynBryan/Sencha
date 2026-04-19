#pragma once

#include <core/service/IService.h>
#include <time/FrameClock.h>
#include <chrono>
#include <cstdint>
#include <vector>

//=============================================================================
// TimescaleHandle
//
// Opaque handle returned by TimeService::PushTimescale(). Pass it back to
// PopTimescale() to remove that entry from the stack. Handles do not need
// to be popped in push order — PopTimescale does a find-and-remove.
//=============================================================================
struct TimescaleHandle
{
	uint32_t Id = 0;
	bool IsValid() const { return Id != 0; }
};

//=============================================================================
// TimeService
//
// Source of truth for all engine timing. Owns a steady_clock and produces a
// FrameClock snapshot each frame via Advance(). Call Advance() exactly once per
// frame, before SystemHost::Update(), then pass the returned snapshot through.
//
// Timescale stack
// ---------------
// PushTimescale() pushes a desired scale onto a stack and returns a handle.
// The active scale is always the top of the stack (most recently pushed that
// hasn't been popped). PopTimescale() removes the entry by handle — pops are
// safe in any order. If the stack is empty the scale falls back to 1.0.
//
// This lets independent systems (pause menu, debug console, slow-motion
// ability) each own their own scale override without caching "last scale" or
// stomping each other's state.
//
// SetTimescale updates the fallback scale used when no stack entries are
// active. GetTimescale always returns the effective scale.
//
// Delta clamping
// --------------
// UnscaledDeltaTime is clamped to MaxDeltaSeconds before scaling. This
// prevents a single long stall (alt-tab, debugger break, first frame) from
// producing a physics or gameplay spike.
//=============================================================================
class TimeService : public IService
{
public:
	TimeService();

	// Advance the clock by one frame. Returns the FrameClock snapshot for
	// this frame. Must be called exactly once per frame, before any systems run.
	FrameClock Advance();

	// Reset the wall-clock baseline without accumulating elapsed time. Use after
	// swapchain/window lifecycle stalls so resize or present blocking does not
	// become simulation time on the next frame.
	void ResetToNow();

	// -- Timescale stack ------------------------------------------------------

	// Push a timescale override. Returns a handle used to remove this entry.
	// The most recently pushed (un-popped) entry determines the active scale.
	TimescaleHandle PushTimescale(float scale);

	// Remove a previously pushed timescale entry. Safe to call in any order.
	// Calling with an invalid or already-popped handle is a no-op.
	void PopTimescale(TimescaleHandle handle);

	// Active scale: top of the stack, or the flat scale if the stack is empty.
	float GetTimescale() const;

	// -- Flat override (no stack) ---------------------------------------------

	// Direct set / get, bypassing the stack. When the stack is non-empty,
	// the stack top takes precedence over this value.
	void  SetTimescale(float scale) { FlatTimescale = scale; }
	float GetFlatTimescale()  const { return FlatTimescale; }

private:
	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;

	// Cap a single-frame delta to ~15 fps equivalent to absorb stalls.
	static constexpr float MaxDeltaSeconds = 1.0f / 15.0f;

	struct StackEntry
	{
		uint32_t Id;
		float    Scale;
	};

	TimePoint LastTime;
	float    FlatTimescale      = 1.0f;
	float    ElapsedTime        = 0.0f;
	float    UnscaledElapsedTime = 0.0f;
	uint64_t FrameIndex         = 0;
	uint32_t NextHandleId       = 1; // 0 is the invalid sentinel
	bool     FirstFrame         = true;

	std::vector<StackEntry> TimescaleStack;
};
