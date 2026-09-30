#include <graphics/RenderFeatureScope.h>

#include <algorithm>

FeatureScopeFault ResolveFeatureScope(RenderPhase phase, RenderFeatureScope requested, PresentationId primary,
                                      bool requestedPresentationLive, RenderFeatureScope& resolved)
{
    const bool swapchainPhase = phase != RenderPhase::Offscreen;
    switch (requested.Kind)
    {
    case RenderFeatureScopeKind::PhaseDefault:
        resolved = swapchainPhase ? RenderFeatureScope::For(primary) : RenderFeatureScope::Global();
        return FeatureScopeFault::None;
    case RenderFeatureScopeKind::Global:
        if (swapchainPhase)
            return FeatureScopeFault::SwapchainPhaseNeedsPresentation;
        resolved = requested;
        return FeatureScopeFault::None;
    case RenderFeatureScopeKind::Presentation:
        if (!requestedPresentationLive)
            return FeatureScopeFault::UnknownPresentation;
        resolved = requested;
        return FeatureScopeFault::None;
    }
    return FeatureScopeFault::UnknownPresentation;
}

bool IsPresentationBound(std::span<const RenderFeatureScope> scopes, PresentationId presentation)
{
    return std::ranges::any_of(scopes, [presentation](const RenderFeatureScope& scope) {
        return scope.Kind == RenderFeatureScopeKind::Presentation && scope.Presentation == presentation;
    });
}
