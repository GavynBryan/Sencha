#pragma once

#include <ecs/ComponentId.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>

#include <cassert>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

// Frozen registration and import recipe for the complete component vocabulary
// of one runtime World.
//
// Engine and game code add concrete component types during module setup, then
// seal the schema. Any runtime World created from that game applies the same
// ordered recipe before its first entity exists. Streamed content resolves
// stable ComponentTypeId values against this schema; it never registers storage
// on a worker or during import.
class WorldComponentSchema
{
public:
    struct Entry
    {
        ComponentTypeId Type;
        std::string_view Name;
        std::size_t Size = 0;
        std::size_t Alignment = 1;
        bool IsTag = false;

    private:
        using RegisterFn = ComponentId (*)(World&);
        using ImportFn = bool (*)(
            World&,
            EntityId,
            std::span<const std::byte>);

        RegisterFn Register = nullptr;
        ImportFn Import = nullptr;
        // Same decode, but into a row that already carries the column. Null for
        // no entry; see WorldComponentSchema::InitializeComponent.
        ImportFn Initialize = nullptr;

        friend class WorldComponentSchema;
    };

    template <typename T>
    bool Add()
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "WorldComponentSchema components must be trivially copyable.");
        assert(!Sealed_ && "Cannot add components after WorldComponentSchema::Seal");

        const ComponentTypeId type = ResolveComponentTypeId<T>();
        const std::size_t size = std::is_empty_v<T> ? 0 : sizeof(T);
        const std::size_t alignment = std::is_empty_v<T> ? 1 : alignof(T);
        const bool isTag = std::is_empty_v<T>;

        if (const Entry* existing = Find(type))
        {
            assert(existing->Size == size
                   && existing->Alignment == alignment
                   && existing->IsTag == isTag
                   && "ComponentTypeId collision in WorldComponentSchema");
            return false;
        }

        assert(Entries_.size() < MaxComponents
               && "WorldComponentSchema exceeds the 256-component budget");

        Entry entry;
        entry.Type = type;
        entry.Name = ResolveComponentName<T>();
        entry.Size = size;
        entry.Alignment = alignment;
        entry.IsTag = isTag;
        entry.Register = [](World& world) {
            return world.RegisterComponent<T>();
        };
        entry.Import = [](World& world,
                          EntityId entity,
                          std::span<const std::byte> bytes) {
            if (world.HasComponent<T>(entity))
                return false;

            if constexpr (std::is_empty_v<T>)
            {
                if (!bytes.empty())
                    return false;
                world.AddComponent<T>(entity);
            }
            else
            {
                if (bytes.size() != sizeof(T))
                    return false;
                T value{};
                std::memcpy(&value, bytes.data(), sizeof(T));
                world.AddComponent<T>(entity, value);
            }
            return true;
        };
        entry.Initialize = [](World& world,
                              EntityId entity,
                              std::span<const std::byte> bytes) {
            if constexpr (std::is_empty_v<T>)
            {
                if (!bytes.empty())
                    return false;
                return world.InitializeComponent<T>(entity);
            }
            else
            {
                if (bytes.size() != sizeof(T))
                    return false;
                T value{};
                std::memcpy(&value, bytes.data(), sizeof(T));
                return world.InitializeComponent<T>(entity, value);
            }
        };
        Entries_.push_back(entry);
        return true;
    }

    void Seal()
    {
        assert(!Sealed_ && "WorldComponentSchema already sealed");
        assert(Entries_.size() <= MaxComponents);
        Sealed_ = true;
    }

    // Applies the exact registration order. A World that already carries the
    // same prefix is tolerated, which keeps editor/test construction flexible;
    // any extra or differently ordered component makes the assigned id diverge
    // and fails loudly.
    void Apply(World& world) const
    {
        assert(Sealed_ && "Apply requires a sealed WorldComponentSchema");
        for (std::size_t index = 0; index < Entries_.size(); ++index)
        {
            const ComponentId assigned = Entries_[index].Register(world);
            assert(assigned == static_cast<ComponentId>(index)
                   && "World component registration order differs from sealed schema");
        }
    }

    // Imports one package component through the concrete component type that was
    // registered during startup. This preserves typed AddComponent semantics,
    // including OnAdd hooks, without putting function pointers in detached data.
    bool ImportComponent(
        World& world,
        EntityId entity,
        ComponentTypeId type,
        std::span<const std::byte> bytes) const
    {
        assert(Sealed_ && "ImportComponent requires a sealed schema");
        const Entry* entry = Find(type);
        return entry != nullptr
            && entry->Import != nullptr
            && entry->Import(world, entity, bytes);
    }

    // Decodes into a row created at its final signature, firing OnAdd exactly as
    // ImportComponent would. Returns false when the row does not carry the
    // column, so the caller can fall back to ImportComponent.
    bool InitializeComponent(
        World& world,
        EntityId entity,
        ComponentTypeId type,
        std::span<const std::byte> bytes) const
    {
        assert(Sealed_ && "InitializeComponent requires a sealed schema");
        const Entry* entry = Find(type);
        return entry != nullptr
            && entry->Initialize != nullptr
            && entry->Initialize(world, entity, bytes);
    }

    [[nodiscard]] const Entry* Find(ComponentTypeId type) const
    {
        for (const Entry& entry : Entries_)
            if (entry.Type == type)
                return &entry;
        return nullptr;
    }

    [[nodiscard]] bool Contains(ComponentTypeId type) const
    {
        return Find(type) != nullptr;
    }

    [[nodiscard]] bool IsSealed() const { return Sealed_; }
    [[nodiscard]] std::size_t Size() const { return Entries_.size(); }
    [[nodiscard]] std::size_t RemainingCapacity() const
    {
        return MaxComponents - Entries_.size();
    }

    [[nodiscard]] std::span<const Entry> Entries() const
    {
        return { Entries_.data(), Entries_.size() };
    }

private:
    std::vector<Entry> Entries_;
    bool Sealed_ = false;
};
