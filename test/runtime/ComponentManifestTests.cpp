#include <gtest/gtest.h>

#include <world/ComponentManifest.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentStorageTraits.h>
#include <world/serialization/SceneSerializer.h>
#include <zone/DefaultZoneBuilder.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace
{
    // Schema defaults must equal member initializers. This is the contract
    // that lets a future reflection-generated TypeSchema read defaults off
    // T{} without silently changing the wire format
    // (docs/ecs/component-registration-plan.md, "C++26 Readiness").
    template <typename T>
    void ExpectDefaultsMatchInitializers()
    {
        const T initialized{};
        auto fields = TypeSchema<T>::Fields();
        std::apply([&](const auto&... field)
        {
            ([&]
            {
                if (!field.DefaultValue.has_value())
                    return;
                EXPECT_TRUE(initialized.*field.Ptr == *field.DefaultValue)
                    << TypeSchema<T>::Name << "." << field.Name
                    << ": schema default differs from the member initializer";
            }(), ...);
        }, fields);
    }
}

TEST(ComponentManifest, ChunkIdsAndJsonNamesAreUnique)
{
    std::vector<std::uint32_t> chunkIds;
    std::vector<std::string_view> jsonNames;
    ForEachSceneComponent([&](auto tag)
    {
        using T = typename decltype(tag)::Type;
        chunkIds.push_back(ComponentStorageTraits<T>::BinaryChunkId);
        jsonNames.push_back(TypeSchema<T>::Name);
    });

    auto sortedIds = chunkIds;
    std::ranges::sort(sortedIds);
    EXPECT_EQ(std::ranges::adjacent_find(sortedIds), sortedIds.end())
        << "Two manifest components share a SceneChunkId";

    auto sortedNames = jsonNames;
    std::ranges::sort(sortedNames);
    EXPECT_EQ(std::ranges::adjacent_find(sortedNames), sortedNames.end())
        << "Two manifest components share a TypeSchema::Name";
}

TEST(ComponentManifest, SchemaDefaultsMatchMemberInitializers)
{
    ForEachSceneComponent([](auto tag)
    {
        ExpectDefaultsMatchInitializers<typename decltype(tag)::Type>();
    });
}

TEST(ComponentManifest, DefaultRegistryRegistersEveryManifestComponent)
{
    Registry registry;
    InitializeDefault3DRegistry(registry);

    ForEachSceneComponent([&](auto tag)
    {
        using T = typename decltype(tag)::Type;
        EXPECT_TRUE(registry.Components.IsRegistered<T>())
            << TypeSchema<T>::Name << " missing from the default registry";
    });

    // LocalTransform's storage traits co-register the derived/hierarchy
    // components that don't serialize as their own chunks.
    EXPECT_TRUE(registry.Components.IsRegistered<WorldTransform>());
    EXPECT_TRUE(registry.Components.IsRegistered<Parent>());
}

TEST(ComponentManifest, InitSceneSerializerIsIdempotentAndCoversManifest)
{
    ClearComponentSerializers();
    InitSceneSerializer();
    const std::size_t count = GetComponentSerializerEntries().size();
    EXPECT_EQ(count, std::tuple_size_v<EngineSceneComponents>);

    InitSceneSerializer();
    EXPECT_EQ(GetComponentSerializerEntries().size(), count)
        << "Re-running InitSceneSerializer must not duplicate entries";
}
