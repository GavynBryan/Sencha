#include "properties/PropertySource.h"

#include "properties/PropertyTree.h"

bool CommitOnlyPropertySource::BeginEdit(std::string_view key)
{
    const std::vector<PropertyNode> nodes = Nodes();
    const PropertyNode* node = FindProperty(nodes, key);
    return node != nullptr && !node->Descriptor.ReadOnly;
}
