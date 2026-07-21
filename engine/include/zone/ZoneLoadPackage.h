#pragma once

#include <ecs/ComponentTypeId.h>
#include <zone/ZoneId.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

// Package-local identity. It is meaningful only inside one ZoneLoadPackage and
// is replaced with a live generational EntityId during owner-thread import.
struct ZoneLocalEntityId
{
    static constexpr std::uint32_t InvalidValue =
        std::numeric_limits<std::uint32_t>::max();

    std::uint32_t Value = InvalidValue;

    [[nodiscard]] bool IsValid() const { return Value != InvalidValue; }
    friend bool operator==(ZoneLocalEntityId, ZoneLocalEntityId) = default;
};

struct ZonePackageComponent
{
    ComponentTypeId Type;
    std::vector<std::byte> Bytes;
};

struct ZonePackageEntity
{
    std::vector<ZonePackageComponent> Components;
};

struct ZonePackageParent
{
    ZoneLocalEntityId Child;
    ZoneLocalEntityId Parent;
};

// Detached, plain CPU representation of one streamed zone. It owns no live
// World, Registry, backend handle, service pointer, or module callback. Workers
// may build and discard it freely; publication happens later on the owner thread.
class ZoneLoadPackage
{
public:
    explicit ZoneLoadPackage(ZoneId zone = {})
        : Zone_(zone)
    {
    }

    [[nodiscard]] ZoneId Zone() const { return Zone_; }
    void SetZone(ZoneId zone) { Zone_ = zone; }

    ZoneLocalEntityId CreateEntity()
    {
        assert(Entities_.size()
               < static_cast<std::size_t>(ZoneLocalEntityId::InvalidValue));
        const auto value = static_cast<std::uint32_t>(Entities_.size());
        Entities_.emplace_back();
        return ZoneLocalEntityId{ value };
    }

    template <typename T>
    bool AddComponent(ZoneLocalEntityId entity, const T& value = T{})
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "Zone package components must be trivially copyable");

        if (!ContainsEntity(entity))
            return false;

        std::span<const std::byte> bytes;
        if constexpr (!std::is_empty_v<T>)
        {
            bytes = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(&value),
                sizeof(T));
        }
        return AddComponentBytes(entity, ResolveComponentTypeId<T>(), bytes);
    }

    bool AddComponentBytes(
        ZoneLocalEntityId entity,
        ComponentTypeId type,
        std::span<const std::byte> bytes)
    {
        if (!ContainsEntity(entity) || !type.IsValid())
            return false;

        ZonePackageEntity& target = Entities_[entity.Value];
        const bool duplicate = std::any_of(
            target.Components.begin(),
            target.Components.end(),
            [type](const ZonePackageComponent& component) {
                return component.Type == type;
            });
        if (duplicate)
            return false;

        ZonePackageComponent component;
        component.Type = type;
        component.Bytes.assign(bytes.begin(), bytes.end());
        target.Components.push_back(std::move(component));
        return true;
    }

    bool SetParent(ZoneLocalEntityId child, ZoneLocalEntityId parent)
    {
        if (!ContainsEntity(child) || !ContainsEntity(parent) || child == parent)
            return false;

        const bool duplicate = std::any_of(
            Parents_.begin(),
            Parents_.end(),
            [child](const ZonePackageParent& relation) {
                return relation.Child == child;
            });
        if (duplicate)
            return false;

        Parents_.push_back(ZonePackageParent{ child, parent });
        return true;
    }

    [[nodiscard]] bool ContainsEntity(ZoneLocalEntityId entity) const
    {
        return entity.IsValid() && entity.Value < Entities_.size();
    }

    [[nodiscard]] std::span<const ZonePackageEntity> Entities() const
    {
        return { Entities_.data(), Entities_.size() };
    }

    [[nodiscard]] std::span<const ZonePackageParent> Parents() const
    {
        return { Parents_.data(), Parents_.size() };
    }

    [[nodiscard]] bool Empty() const { return Entities_.empty(); }
    [[nodiscard]] std::size_t EntityCount() const { return Entities_.size(); }

private:
    ZoneId Zone_;
    std::vector<ZonePackageEntity> Entities_;
    std::vector<ZonePackageParent> Parents_;
};
