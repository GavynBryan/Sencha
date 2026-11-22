#pragma once

#include <core/identity/Id.h>
#include <core/json/JsonValue.h>
#include <ecs/ComponentTypeId.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

// Package-local identity. It is meaningful only inside one EntityBuildPackage
// and is replaced with a live generational EntityId during owner-thread import.
struct PackageEntityId
{
    static constexpr std::uint32_t InvalidValue =
        std::numeric_limits<std::uint32_t>::max();

    std::uint32_t Value = InvalidValue;

    [[nodiscard]] bool IsValid() const { return Value != InvalidValue; }
    friend bool operator==(PackageEntityId, PackageEntityId) = default;
};

struct PackageComponent
{
    ComponentTypeId Type;

    // Exactly one payload form is populated. RuntimeBytes are useful for
    // generated/package-native content. SerializedJson is parsed on a worker but
    // decoded on the owner thread, where asset resolution and other explicit
    // SceneSerializationContext dependencies are legal.
    std::vector<std::byte> RuntimeBytes;
    std::optional<JsonValue> SerializedJson;

    [[nodiscard]] bool HasRuntimeBytes() const
    {
        return !SerializedJson.has_value();
    }
};

struct PackageEntity
{
    std::vector<PackageComponent> Components;

    // Content's form of each component a saved patch replaced, decoded when the patched one does not.
    std::vector<PackageComponent> Unpatched;

    // The entity's persistent identity, extracted at package build so the
    // owner-thread import can consult persisted state before creating the row.
    // Invalid for entities without identity (cook-generated content). The
    // persistent_id component still travels in Components; this field is import
    // metadata, not a second source of truth.
    PersistentEntityId PersistentId;

    PersistentEntityId MemberId;

    // Set by SetParent; a package parent is single and final.
    bool HasParent = false;

    [[nodiscard]] const PackageComponent* Find(ComponentTypeId type) const
    {
        const auto found = std::find_if(Components.begin(), Components.end(),
                                        [type](const PackageComponent& component) { return component.Type == type; });
        return found != Components.end() ? &*found : nullptr;
    }

    [[nodiscard]] PackageComponent* Find(ComponentTypeId type)
    {
        return const_cast<PackageComponent*>(std::as_const(*this).Find(type));
    }

    // Replaces the component's payload with `value`, or adds it.
    void SetSerialized(ComponentTypeId type, JsonValue value)
    {
        PackageComponent* found = Find(type);
        PackageComponent& target = found != nullptr ? *found : Components.emplace_back();
        target.Type = type;
        target.RuntimeBytes.clear();
        target.SerializedJson = std::move(value);
    }

    bool Remove(ComponentTypeId type)
    {
        return std::erase_if(Components, [type](const PackageComponent& component) { return component.Type == type; }) > 0;
    }
};

struct PackageParent
{
    PackageEntityId Child;
    PackageEntityId Parent;
};

// Detached, plain CPU representation of a set of entities to be built into a
// World. It owns no live World, Registry, backend handle, service pointer, or
// module callback. Workers may build and discard it freely; publication happens
// later on the owner thread.
//
// The package describes entities, not where they came from. What the destination
// does with them -- which storage partition receives them, and whether persisted
// per-zone state suppresses any of them -- belongs to the importing caller, which
// already knows both.
class EntityBuildPackage
{
public:
    PackageEntityId CreateEntity()
    {
        assert(Entities_.size()
               < static_cast<std::size_t>(PackageEntityId::InvalidValue));
        const auto value = static_cast<std::uint32_t>(Entities_.size());
        Entities_.emplace_back();
        return PackageEntityId{ value };
    }

    template <typename T>
    bool AddComponent(PackageEntityId entity, const T& value = T{})
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "Package components must be trivially copyable");

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
        PackageEntityId entity,
        ComponentTypeId type,
        std::span<const std::byte> bytes)
    {
        PackageEntity* target = FindInsertTarget(entity, type);
        if (target == nullptr)
            return false;

        PackageComponent component;
        component.Type = type;
        component.RuntimeBytes.assign(bytes.begin(), bytes.end());
        target->Components.push_back(std::move(component));
        return true;
    }

    bool AddSerializedJson(
        PackageEntityId entity,
        ComponentTypeId type,
        JsonValue value)
    {
        PackageEntity* target = FindInsertTarget(entity, type);
        if (target == nullptr)
            return false;

        PackageComponent component;
        component.Type = type;
        component.SerializedJson = std::move(value);
        target->Components.push_back(std::move(component));
        return true;
    }

    bool SetPersistentId(PackageEntityId entity, PersistentEntityId id)
    {
        if (!ContainsEntity(entity))
            return false;
        Entities_[entity.Value].PersistentId = id;
        Entities_[entity.Value].MemberId = id;
        return true;
    }

    bool SetMemberId(PackageEntityId entity, PersistentEntityId id)
    {
        if (!ContainsEntity(entity))
            return false;
        Entities_[entity.Value].MemberId = id;
        return true;
    }

    bool SetParent(PackageEntityId child, PackageEntityId parent)
    {
        if (!ContainsEntity(child) || !ContainsEntity(parent) || child == parent)
            return false;
        // The per-entity flag keeps the duplicate check O(1); a scan of
        // Parents_ would make a full parent pass quadratic.
        if (Entities_[child.Value].HasParent)
            return false;

        Entities_[child.Value].HasParent = true;
        Parents_.push_back(PackageParent{ child, parent });
        return true;
    }

    [[nodiscard]] const PackageComponent* FindComponent(PackageEntityId entity, ComponentTypeId type) const
    {
        return ContainsEntity(entity) ? Entities_[entity.Value].Find(type) : nullptr;
    }

    // Replaces the component's payload with `value`, or adds it.
    bool SetSerializedComponent(PackageEntityId entity, ComponentTypeId type, JsonValue value)
    {
        if (!ContainsEntity(entity) || !type.IsValid())
            return false;
        Entities_[entity.Value].SetSerialized(type, std::move(value));
        return true;
    }

    bool RemoveComponent(PackageEntityId entity, ComponentTypeId type)
    {
        return ContainsEntity(entity) && Entities_[entity.Value].Remove(type);
    }

    [[nodiscard]] PackageEntity* FindEntity(PackageEntityId entity)
    {
        return ContainsEntity(entity) ? &Entities_[entity.Value] : nullptr;
    }

    [[nodiscard]] bool ContainsEntity(PackageEntityId entity) const
    {
        return entity.IsValid() && entity.Value < Entities_.size();
    }

    [[nodiscard]] std::span<const PackageEntity> Entities() const
    {
        return { Entities_.data(), Entities_.size() };
    }

    [[nodiscard]] std::span<const PackageParent> Parents() const
    {
        return { Parents_.data(), Parents_.size() };
    }

    [[nodiscard]] bool Empty() const { return Entities_.empty(); }
    [[nodiscard]] std::size_t EntityCount() const { return Entities_.size(); }

private:
    PackageEntity* FindInsertTarget(
        PackageEntityId entity,
        ComponentTypeId type)
    {
        if (!ContainsEntity(entity) || !type.IsValid())
            return nullptr;

        PackageEntity& target = Entities_[entity.Value];
        const bool duplicate = std::any_of(
            target.Components.begin(),
            target.Components.end(),
            [type](const PackageComponent& component) {
                return component.Type == type;
            });
        return duplicate ? nullptr : &target;
    }

    std::vector<PackageEntity> Entities_;
    std::vector<PackageParent> Parents_;
};
