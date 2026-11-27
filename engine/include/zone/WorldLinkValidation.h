#pragma once

#include <zone/ContentRiskRecord.h>
#include <zone/WorldTopologyCook.h>

#include <span>
#include <vector>

// Whether each authored link is well formed against the apertures collected
// from `collectedZones`: link records sorted by link id then rule, then the
// unlinked-aperture warnings by aperture id. A reference into a zone that was
// not collected is not judged. docs/zone/links.md, "Validation".
[[nodiscard]] std::vector<ContentRiskRecord> ValidateAuthoredLinks(
    const WorldPartitionManifest& manifest,
    std::span<const AuthoredDockCookInput> docks,
    std::span<const AuthoredLinkCookInput> links,
    std::span<const AuthoredApertureCookInput> apertures,
    std::span<const ZoneId> collectedZones);
