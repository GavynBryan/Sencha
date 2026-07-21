#pragma once

#include <ecs/ComponentId.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>

#include <cassert>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

// Frozen registration recipe for the complete component vocabulary of one
// runtime World.
//
// Engine and game code add concrete component types during module setup, then
// seal the schema. Any runtime World created from that game applies the same
// ordered recipe before its first entity exists. Streamed content resolves
// stable ComponentTypeId values against the resulting World; it never registers
// storage on a worker or during import.
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
        RegisterFn Register = nullptr;

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
