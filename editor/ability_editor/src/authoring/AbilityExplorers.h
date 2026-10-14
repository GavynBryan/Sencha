#pragma once

#include "project/ProjectFiles.h"
#include "workspaces/TreeOutline.h"

#include <core/json/JsonValue.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The project's abilities as an outline, keyed by asset path.
[[nodiscard]] std::vector<OutlineNode> AbilityAssetNodes(std::span<const ProjectFile> abilities, std::string_view open);

// An ability's elements as an outline, keyed by element key: its conditions,
// locals and costs, then each phase with its ops and transitions under it. A
// group's key starts with kAbilityGroupKey and is opened rather than chosen.
inline constexpr std::string_view kAbilityGroupKey = "group:";
[[nodiscard]] std::vector<OutlineNode> AbilityElementNodes(const JsonValue& data, std::string_view selected);
