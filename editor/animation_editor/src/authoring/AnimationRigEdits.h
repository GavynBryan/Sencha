#pragma once

#include <anim/AnimRigBinding.h>
#include <core/json/JsonValue.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

//=============================================================================
// Rig edits
//
// The bone mask operations the skeleton tree offers, over a rig document's
// root. Like the selector edits, each is a pure edit of the authored JSON the
// panels apply inside a document transaction, one undo step each, and the
// tests drive without a GUI. A step is written as briefly as it reads: the
// joint, then `exclude` and `subtree` only where they differ from their
// defaults.
//=============================================================================

[[nodiscard]] JsonValue::Array* AnimRigLayers(JsonValue& root);

bool AddAnimMaskStep(JsonValue& root, std::size_t layer, std::string joint, bool exclude, bool subtree);
bool RemoveAnimMaskStep(JsonValue& root, std::size_t layer, std::size_t step);
bool ClearAnimMask(JsonValue& root, std::size_t layer);

// Per joint of the rig's skeleton, one bit per layer that covers it: every
// joint for an unmasked layer. Empty when the rig has no skeleton bound.
[[nodiscard]] std::vector<std::uint8_t> AnimMaskCoverage(const AnimBoundRig& rig);
