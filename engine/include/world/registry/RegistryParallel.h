#pragma once

#include <jobs/JobSystem.h>

#include <cstdint>
#include <span>

class Registry;

//=============================================================================
// ForEachRegistryParallel
//
// Zone-axis frame parallelism (docs/ecs/parallelization.md, Decision 4): runs
// fn(Registry&) once per non-null registry in the span, in parallel when the
// span has more than one entry. Disjoint registries cannot alias, so this is
// safe by construction — no access metadata, no locks.
//
// Rules the callback must honor (the standard frame-lane job contract):
//   - Touch only the registry it is handed. The global registry is read-only
//     inside the span; write global state before the fork or after the join.
//   - Record command buffers per registry inside the job; flush them serially
//     after the join, on the calling thread.
//   - No nested ParallelFor: zone-axis and chunk-axis parallelism do not
//     compose. Pick the axis that matches the workload shape.
//
// Entries must be distinct (FrameRegistryView spans are by construction);
// duplicate pointers would alias across jobs. Callers that cannot guarantee
// distinctness must deduplicate before forking.
//
// A span of zero or one entries runs inline on the caller: the pool dispatch
// floor (~300 µs measured, see the Stage A results) is never paid for a
// workload that cannot parallelize.
//=============================================================================
template <typename Fn>
void ForEachRegistryParallel(JobSystem& jobs, std::span<Registry* const> registries, Fn&& fn)
{
    if (registries.size() <= 1)
    {
        if (!registries.empty() && registries[0] != nullptr)
        {
            fn(*registries[0]);
        }
        return;
    }

    jobs.ParallelFor(static_cast<uint32_t>(registries.size()), [&](uint32_t index) {
        if (registries[index] != nullptr)
        {
            fn(*registries[index]);
        }
    });
}
