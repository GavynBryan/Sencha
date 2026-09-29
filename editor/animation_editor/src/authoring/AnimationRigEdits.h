#pragma once

#include <anim/AnimRigBinding.h>
#include <core/json/JsonValue.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

[[nodiscard]] JsonValue::Array* AnimRigLayers(JsonValue& root);

// Writes `exclude` and `subtree` only where they differ from their defaults.
bool AddAnimMaskStep(JsonValue& root, std::size_t layer, std::string joint, bool exclude, bool subtree);
bool RemoveAnimMaskStep(JsonValue& root, std::size_t layer, std::size_t step);
bool ClearAnimMask(JsonValue& root, std::size_t layer);

// Per joint, one bit per layer covering it. Empty when no skeleton is bound.
[[nodiscard]] std::vector<std::uint8_t> AnimMaskCoverage(const AnimBoundRig& rig);
