#pragma once

//=============================================================================
// FrameTime
//
// Immutable per-frame time snapshot produced by TimeService::Advance() and
// threaded down to every system via SystemHost::Update(). All fields are
// pre-computed so systems pay no cost beyond reading what they need.
//
// Layout: 5 floats (20 bytes). Fits in a single cache line.
//
// DeltaTime / ElapsedTime      — scaled by Timescale. Use for gameplay logic.
// UnscaledDeltaTime / Elapsed  — raw wall-clock values. Use for UI, audio,
//                                debug overlays, or anything that must ignore
//                                slow-motion / fast-forward.
//=============================================================================
struct FrameTime
{
	float DeltaTime;
	float UnscaledDeltaTime;
	float ElapsedTime;
	float UnscaledElapsedTime;
	float Timescale;
};
