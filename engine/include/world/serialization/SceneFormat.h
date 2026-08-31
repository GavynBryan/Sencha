#pragma once

#include <cstdint>

// The version stamped on serialized scene JSON (editor documents, the cook's
// passthrough form) and checked on read. Cooked runtime scenes carry their own
// versioned container (.smap); this covers only the JSON shape.
constexpr std::uint32_t SceneVersion = 1;
