#pragma once

#include <compare>
#include <cstdint>

// Dense runtime identity for one storage partition inside an ECS World.
//
// The ECS layer deliberately does not know what a partition represents. Runtime
// world partition maps resident zones to these ids; editor documents and tests
// may use them for other disjoint storage lanes. Value zero is the default
// partition used by every existing unqualified World API during migration.
// The 16-bit representation bounds all partition-indexed tables and matches the
// intended resident-zone slot model; authored ZoneId values are never stored here.
struct StoragePartitionId
{
    std::uint16_t Value = 0;

    [[nodiscard]] static constexpr StoragePartitionId Default()
    {
        return StoragePartitionId{ 0 };
    }

    friend constexpr bool operator==(StoragePartitionId, StoragePartitionId) = default;
    friend constexpr auto operator<=>(StoragePartitionId, StoragePartitionId) = default;
};
