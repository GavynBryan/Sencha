#pragma once

#include <zone/ContentRiskRecord.h>

#include <optional>
#include <vector>

class EditorScene;

// The body that has to pass through openings: the navigation profile's capsule.
struct OpeningClearance
{
    float Radius = 0.0f;
    float Height = 0.0f;
};

// What is wrong with the level's spaces and openings, each record about an
// entity: shells refused or approximate, spaces that overlap, openings that
// could not be cut, lead nowhere, or are too small for `clearance`.
[[nodiscard]] std::vector<ContentRiskRecord> SpaceDiagnostics(const EditorScene& scene,
                                                              const std::optional<OpeningClearance>& clearance);
