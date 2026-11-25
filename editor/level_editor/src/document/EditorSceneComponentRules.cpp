#include "EditorSceneComponentRules.h"

#include "DerivedComponents.h"
#include "DocumentSerialization.h"

#include <ecs/World.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/IComponentSerializer.h>

const DataFieldSchema* EditorSceneComponentRules::SchemaOf(std::string_view component) const
{
    const IComponentSerializer* serializer = EditorSceneSerializers().FindByJsonKey(component);
    return serializer != nullptr ? serializer->CompositionSchema() : nullptr;
}

std::vector<std::string> EditorSceneComponentRules::OwedBy(std::string_view component) const
{
    const ComponentSerializerRegistry& serializers = EditorSceneSerializers();
    const IComponentSerializer* serializer = serializers.FindByJsonKey(component);
    if (serializer == nullptr)
        return {};
    const ComponentId id = Components.GetComponentIdByType(serializer->TypeId());
    if (id == InvalidComponentId)
        return {};
    std::vector<std::string> owed;
    for (const ComponentId closure : DerivedClosureOf(Components, id))
        for (const auto& candidate : serializers.Entries())
            if (candidate->Authorable() && Components.GetComponentIdByType(candidate->TypeId()) == closure)
                owed.emplace_back(candidate->JsonKey());
    return owed;
}

bool EditorSceneComponentRules::IsRemovable(std::string_view component) const
{
    const IComponentSerializer* serializer = EditorSceneSerializers().FindByJsonKey(component);
    return serializer == nullptr || serializer->IsRemovable();
}
