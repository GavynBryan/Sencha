#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class ReplicationDeferredReferences
{
public:
    static constexpr std::size_t kCapacity = 64;

    struct Waiting
    {
        EntityId Holder;
        ComponentTypeId Type;
        std::vector<std::byte> Image;
        std::vector<std::uint64_t> Unresolved;
    };

    void Remember(EntityId holder, ComponentTypeId type, std::span<const std::byte> image,
                  std::span<const std::uint64_t> unresolved);
    void ForgetEntity(EntityId holder);
    [[nodiscard]] const Waiting* Find(EntityId holder, ComponentTypeId type) const;
    void Clear() { Entries.clear(); }

    [[nodiscard]] std::size_t Size() const { return Entries.size(); }
    [[nodiscard]] std::uint64_t Forgotten() const { return Evicted; }
    [[nodiscard]] std::span<const Waiting> All() const { return Entries; }

private:
    std::vector<Waiting> Entries;
    std::uint64_t Evicted = 0;
};
