#pragma once

#include <vector>

#include <zone/ContentRiskRecord.h>
#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionManifest.h>

// Manifest-level validation. Pure: no logging, no file IO, no engine services.
// Emits records in deterministic order: rule table order, then ascending source
// id within a rule. Rules that need loaded zone content are not here; they
// belong to the layers that load content.
[[nodiscard]] std::vector<ContentRiskRecord>
ValidateWorldPartitionManifest(const WorldPartitionManifest& manifest,
                               const WorldPartitionIndex& index);
