#pragma once

#include <audio/AudioClip.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

//=============================================================================
// .sclip round trip (docs/assets/pipeline.md, Decision F).
//
// Both halves are pure functions (no logging, no engine state): the write
// half runs inside importers, which report errors instead of logging; the
// read half runs inside the audio stage half, which may be on a task
// thread. Small enough that splitting them into Serializer/Loader files
// (the .stex precedent) would be ceremony.
//=============================================================================

// Writes `clip` as a .sclip container. Returns false on an invalid clip
// (no samples, zero rate or channels, sample count not divisible by the
// channel count).
[[nodiscard]] bool WriteSclipToBytes(const AudioClip& clip, std::vector<std::byte>& out);

// Parses a .sclip container. Rejects malformed input (bad magic, unknown
// version, header/data bounds that disagree) — a malformed container is
// rejected, never patched. Errors travel in `error`.
[[nodiscard]] bool LoadSclipFromBytes(std::span<const std::byte> bytes,
                                      AudioClip& out,
                                      std::string* error = nullptr);
