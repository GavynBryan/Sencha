#pragma once

#include <authored/VerbInvocation.h>
#include <core/metadata/DataSchema.h>

#include <cstddef>
#include <cstdint>
#include <string>

class FixedSimulationLoop;

// What every camera effect verb takes and how it reads its clock.

// The optional `subject`: only the view this entity owns presents the effect.
[[nodiscard]] DataFieldSchema CameraEffectSubjectField();

// An optional non-negative span of an envelope, in seconds, defaulting to none.
[[nodiscard]] DataFieldSchema CameraEffectSecondsField(std::string key, std::string label, std::string summary);
[[nodiscard]] bool ReadCameraEffectSeconds(const AuthoredArguments& arguments, std::size_t slot, float& out);

// The tick an effect starts or releases at: the invocation's, or, from a
// producer on the frame clock, the tick in progress.
[[nodiscard]] std::uint64_t CameraEffectTickOf(const VerbInvocation& invocation, const FixedSimulationLoop& clock);
