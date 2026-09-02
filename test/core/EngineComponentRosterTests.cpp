// The engine's component roster, three ways: what its features name, what it
// registers, and what the generator found annotated. Registration is
// idempotent and cannot tell a second claim from a first, so the first two
// are compared as the ordered lists the features state; the generated index
// is what catches a component annotated but never registered, or registered
// but never annotated.

#include <ecs/ComponentTypeId.h>
#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeComponentSchema.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    struct IndexedComponent
    {
        std::string Type;
        std::string Identity;
    };

    // Records are tab-separated: Type, Identity, SchemaName, SceneChunk, Header, Line.
    std::vector<IndexedComponent> ReadComponentIndex(const char* path)
    {
        std::ifstream file(path);
        EXPECT_TRUE(file.is_open()) << "engine component index not found at " << path;

        std::vector<IndexedComponent> components;
        for (std::string line; std::getline(file, line);)
        {
            std::istringstream fields(line);
            IndexedComponent component;
            std::getline(fields, component.Type, '\t');
            std::getline(fields, component.Identity, '\t');
            components.push_back(std::move(component));
        }
        return components;
    }

    std::map<ComponentTypeId, std::string> NamesByType(const WorldComponentSchema& schema)
    {
        std::map<ComponentTypeId, std::string> names;
        for (const WorldComponentSchema::Entry& entry : schema.Entries())
            names.emplace(entry.Type, std::string(entry.Name));
        return names;
    }
}

TEST(EngineComponentRoster, FeaturesNameExactlyWhatTheEngineRegisters)
{
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);

    std::vector<ComponentTypeId> registered;
    for (const WorldComponentSchema::Entry& entry : schema.Entries())
        registered.push_back(entry.Type);

    const std::span<const ComponentTypeId> named = EngineComponentIds();
    EXPECT_TRUE(std::equal(named.begin(), named.end(), registered.begin(), registered.end()))
        << "the feature vocabularies and RegisterEngineComponents disagree on "
           "which components the engine registers, or in what order";
}

TEST(EngineComponentRoster, EveryRegisteredComponentIsAnnotatedAndEveryAnnotatedOneRegistered)
{
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    const std::map<ComponentTypeId, std::string> registered = NamesByType(schema);

    std::map<ComponentTypeId, std::string> annotated;
    for (const IndexedComponent& component : ReadComponentIndex(SENCHA_ENGINE_COMPONENT_INDEX))
        annotated.emplace(MakeComponentTypeId(component.Identity), component.Type);

    for (const auto& [type, name] : registered)
        EXPECT_TRUE(annotated.contains(type))
            << name << " is registered but its header is not run through the generator";

    for (const auto& [type, name] : annotated)
        EXPECT_TRUE(registered.contains(type))
            << name << " is annotated but no feature registers it";
}
