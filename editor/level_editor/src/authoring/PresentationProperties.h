#pragma once

#include "properties/PropertyNode.h"

#include <render/PresentationLayers.h>
#include <render/PresentationSettings.h>

#include <optional>
#include <span>
#include <string_view>

// Rows for one layer of presentation settings, built from its schema. Each row
// shows the value in force, and where it is inherited, which of `below` sets it.
[[nodiscard]] PropertyNode PresentationProperties(const PresentationSettingsLayer& layer,
                                                  std::span<const PresentationLayerEntry> below);

[[nodiscard]] bool IsPresentationKey(std::string_view key);

// The layer after editing one of its rows; nullopt when `value` is not one the
// row takes. An empty value, or Inherit, gives the field back to the layer below.
[[nodiscard]] std::optional<PresentationSettingsLayer> EditPresentationLayer(const PresentationSettingsLayer& layer,
                                                                            std::string_view key,
                                                                            std::string_view value);
