#pragma once

#include <authored/VerbInvocation.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>

class FixedSimulationLoop;

// What every camera effect verb takes and how it reads its clock.

// The optional `subject`: only the view this entity owns presents the effect.
[[nodiscard]] DataFieldSchema CameraEffectSubjectField();

// The tick an effect starts or releases at: the invocation's, or, from a
// producer on the frame clock, the tick in progress.
[[nodiscard]] std::uint64_t CameraEffectTickOf(const VerbInvocation& invocation, const FixedSimulationLoop& clock);
