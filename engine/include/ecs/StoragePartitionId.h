#pragma once

#include <cstdint>

// Dense runtime identity for one storage partition inside an ECS World.
//
// The ECS layer deliberately does not know what a partition represents. Runtime
// world partition maps resident zones to these ids; editor documents and tests
// may use them for other disjoint storage lanes. Value zero is the default
// partition used by every existing unqualified World API during migration.
struct StoragePartitionId
{
    uint32_t Value = 0;

    [[nodiscard]] static constexpr StoragePartitionId Default()
    {
        return StoragePartitionId{ 0 };
    }

    friend constexpr bool operator==(StoragePartitionId, StoragePartitionId) = default;
    friend constexpr auto operator<=>(StoragePartitionId, StoragePartitionId) = default;
};
