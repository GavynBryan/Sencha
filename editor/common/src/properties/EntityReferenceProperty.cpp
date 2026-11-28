#include "properties/EntityReferenceProperty.h"

PropertyNode EntityReferenceProperty(std::string key, std::string label, std::string value, std::string none,
                                     std::string tooltip)
{
    PropertyNode node;
    node.Key = std::move(key);
    node.Descriptor.Kind = PropertyKind::EntityReference;
    node.Descriptor.Label = std::move(label);
    node.Descriptor.Tooltip = std::move(tooltip);
    if (value.empty())
        node.Reading = std::move(none);
    else
        node.Descriptor.Verbs.push_back({ .Id = std::string(EntityReferenceVerbs::Clear), .Label = "Clear" });
    node.Value = std::move(value);
    return node;
}
