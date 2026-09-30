#pragma once

#include <graphics/PresentationId.h>
#include <graphics/RenderFeature.h>

#include <cstdint>
#include <span>

enum class RenderFeatureScopeKind : std::uint8_t
{
    // The primary presentation for a swapchain phase, Global for Offscreen.
    PhaseDefault,
    Global,
    Presentation,
};

// Where a feature records: once per frame, or into one presentation. A
// presentation-scoped Offscreen feature is skipped on frames its presentation
// is not acquired, so a minimized window's previews cost nothing.
struct RenderFeatureScope
{
    RenderFeatureScopeKind Kind = RenderFeatureScopeKind::PhaseDefault;
    PresentationId Presentation{};

    [[nodiscard]] static RenderFeatureScope Global()
    {
        return { RenderFeatureScopeKind::Global, {} };
    }
    [[nodiscard]] static RenderFeatureScope For(PresentationId presentation)
    {
        return { RenderFeatureScopeKind::Presentation, presentation };
    }
    bool operator==(const RenderFeatureScope&) const = default;
};

enum class FeatureScopeFault : std::uint8_t
{
    None,
    // A swapchain phase records inside some presentation's scope.
    SwapchainPhaseNeedsPresentation,
    UnknownPresentation,
};

// Resolves PhaseDefault and checks the result; `resolved` is Global or
// Presentation when the fault is None.
[[nodiscard]] FeatureScopeFault ResolveFeatureScope(RenderPhase phase, RenderFeatureScope requested,
                                                    PresentationId primary, bool requestedPresentationLive,
                                                    RenderFeatureScope& resolved);

// Whether any resolved scope records into `presentation`.
[[nodiscard]] bool IsPresentationBound(std::span<const RenderFeatureScope> scopes, PresentationId presentation);
