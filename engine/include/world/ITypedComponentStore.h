#pragma once

#include <world/IComponentStore.h>
#include <world/entity/EntityId.h>

#include <cstddef>
#include <cstdint>
#include <span>

//=============================================================================
// ITypedComponentStore<T>
//
// Typed component-store contract for systems and tooling that need a uniform
// view over dense component data without knowing the concrete store type.
//
// Owner ids are sparse-set keys (currently EntityId::Index), not full EntityId
// values. Serialization can resolve full entity identity through EntityRegistry
// when it needs generation-aware records.
//=============================================================================
template <typename T>
class ITypedComponentStore : public IComponentStore
{
public:
    ~ITypedComponentStore() override = default;

    virtual bool Add(EntityId entity, const T& component) = 0;
    virtual bool Remove(EntityId entity) = 0;

    [[nodiscard]] virtual T* TryGet(EntityId entity) = 0;
    [[nodiscard]] virtual const T* TryGet(EntityId entity) const = 0;

    [[nodiscard]] virtual std::span<T> GetItems() = 0;
    [[nodiscard]] virtual std::span<const T> GetItems() const = 0;

    [[nodiscard]] virtual std::span<const EntityIndex> GetOwnerIds() const = 0;

    [[nodiscard]] virtual size_t Count() const = 0;
    [[nodiscard]] virtual bool IsEmpty() const = 0;
    [[nodiscard]] virtual uint64_t GetVersion() const = 0;
};
