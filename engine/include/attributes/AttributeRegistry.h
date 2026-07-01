#pragma once

#include <attributes/AttributeId.h>

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

//=============================================================================
// AttributeRegistry
//
// Data-driven attribute definitions: name, clamp range, and default base value.
// Installed as a world resource; serialization resolves names<->ids through it
// so persisted scenes survive registration-order changes. Heap-backed by design
// (one instance, not per-entity); the per-entity values live in AttributeSet.
//=============================================================================
class AttributeRegistry
{
public:
    AttributeRegistry();

    // Registers an attribute by name. Re-registering an existing name returns the
    // existing id and ignores the new parameters (first registration wins).
    // Returns an invalid id for an empty name.
    AttributeId RegisterAttribute(std::string_view name,
                                  float min = std::numeric_limits<float>::lowest(),
                                  float max = std::numeric_limits<float>::max(),
                                  float defaultBase = 0.0f);

    [[nodiscard]] AttributeId FindAttribute(std::string_view name) const;
    [[nodiscard]] std::string_view GetName(AttributeId id) const;
    [[nodiscard]] bool IsKnown(AttributeId id) const;

    [[nodiscard]] float Min(AttributeId id) const;
    [[nodiscard]] float Max(AttributeId id) const;
    [[nodiscard]] float DefaultBase(AttributeId id) const;

    // Clamp `value` to the attribute's [min, max]; returns value unchanged for
    // an unknown id.
    [[nodiscard]] float Clamp(AttributeId id, float value) const;

    [[nodiscard]] std::size_t Size() const;

private:
    struct AttributeDef
    {
        std::string Name;
        float Min = std::numeric_limits<float>::lowest();
        float Max = std::numeric_limits<float>::max();
        float DefaultBase = 0.0f;
    };

    std::vector<AttributeDef> Attrs;                       // [0] is a dummy: id 0 is invalid
    std::unordered_map<std::string, AttributeId> IdsByName;
};
