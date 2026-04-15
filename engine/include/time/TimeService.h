#pragma once

#include <core/service/IService.h>
#include <time/FrameTime.h>
#include <chrono>

//=============================================================================
// TimeService
//
// Source of truth for all engine timing. Owns a steady_clock and produces a
// FrameTime snapshot each frame via Advance(). Call Advance() exactly once per
// frame, before SystemHost::Update(), then pass the returned snapshot through.
//
// Timescale
// ---------
// SetTimescale() scales DeltaTime and ElapsedTime without affecting their
// unscaled counterparts. Timescale of 0 pauses simulation while unscaled time
// continues, useful for pause menus or cinematic freeze-frames.
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

	// Advance the clock by one frame. Returns the FrameTime snapshot for
	// this frame. Must be called exactly once per frame, before any systems run.
	FrameTime Advance();

	void SetTimescale(float scale) { Timescale = scale; }
	float GetTimescale() const { return Timescale; }

private:
	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;

	// Cap a single-frame delta to ~15 fps equivalent to absorb stalls.
	static constexpr float MaxDeltaSeconds = 1.0f / 15.0f;

	TimePoint LastTime;
	float Timescale = 1.0f;
	float ElapsedTime = 0.0f;
	float UnscaledElapsedTime = 0.0f;
	bool FirstFrame = true;
};
