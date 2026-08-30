#pragma once

#include <ecs/ComponentId.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
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

        // Everything this component owes, transitively, computed at Seal from
        // the ComponentTraits declarations. The typed add walks the same graph
        // at compile time; this is the by-id copy, for a caller composing an
        // entity's whole signature before its row exists.
        std::vector<ComponentTypeId> Owed;

    private:
        // As declared, before the closure: what Seal folds together.
        std::vector<ComponentTypeId> DeclaredOwed;

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
        // Overwrite in place on a row that already carries the column. See
        // WorldComponentSchema::SetComponentBytes.
        ImportFn Write = nullptr;
        // The type's own default-constructed bytes. See
        // WorldComponentSchema::WriteDefaultBytes.
        using DefaultsFn = bool (*)(std::span<std::byte>);
        DefaultsFn Defaults = nullptr;
        // Takes the column off the entity. See
        // WorldComponentSchema::RemoveComponent.
        using RemoveFn = bool (*)(World&, EntityId);
        RemoveFn Remove = nullptr;

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
        entry.Write = [](World& world,
                         EntityId entity,
                         std::span<const std::byte> bytes) {
            if constexpr (std::is_empty_v<T>)
            {
                // A tag carries nothing to overwrite; presence is its whole
                // value, and changing that is structural, not a write.
                return bytes.empty() && world.HasComponent<T>(entity);
            }
            else
            {
                if (bytes.size() != sizeof(T))
                    return false;
                // Non-const TryGet publishes the column's write version, so a
                // value arriving this way is visible to Changed<T> exactly as a
                // system's own write would be.
                T* target = world.TryGet<T>(entity);
                if (target == nullptr)
                    return false;
                std::memcpy(target, bytes.data(), sizeof(T));
                return true;
            }
        };
        entry.Defaults = [](std::span<std::byte> bytes) {
            if constexpr (std::is_empty_v<T>)
            {
                return bytes.empty();
            }
            else
            {
                if (bytes.size() != sizeof(T))
                    return false;
                const T value{};
                std::memcpy(bytes.data(), &value, sizeof(T));
                return true;
            }
        };
        entry.Remove = [](World& world, EntityId entity) {
            if (!world.HasComponent<T>(entity))
                return false;
            world.RemoveComponent<T>(entity);
            return true;
        };
        entry.DeclaredOwed = DeclaredOwedIds<T>();
        Entries_.push_back(std::move(entry));
        return true;
    }

    void Seal()
    {
        assert(!Sealed_ && "WorldComponentSchema already sealed");
        assert(Entries_.size() <= MaxComponents);
        ComputeOwedClosures();
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
            [[maybe_unused]] const ComponentId assigned = Entries_[index].Register(world);
            assert(assigned == static_cast<ComponentId>(index)
                   && "World component registration order differs from sealed schema");
        }
    }

    // Fills `bytes` with a default-constructed instance of the component.
    //
    // What a field's absence means. A decoder that leaves unmentioned fields
    // alone needs something underneath them, and for a value arriving on an
    // entity that does not hold the component yet, zero is not it: zero is a
    // number the type never chose, while the member initializers are the values
    // it declares for exactly this case. Substituting zero silently produces a
    // component that is valid, wrong, and inert -- the failure that reads as a
    // feature not working rather than as data being missing.
    bool WriteDefaultBytes(ComponentTypeId type, std::span<std::byte> bytes) const
    {
        const Entry* entry = Find(type);
        return entry != nullptr
            && entry->Defaults != nullptr
            && entry->Defaults(bytes);
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

    // Overwrites a component that is already on the entity, without adding,
    // removing, or firing a lifecycle hook. This is how replication applies a
    // snapshot: the value changes, the entity's shape does not.
    //
    // Deliberately not a fallback for ImportComponent. A component whose
    // ComponentTraits retain external handles cannot be overwritten this way --
    // OnAdd would not run for the incoming handles and OnRemove would not run
    // for the outgoing ones -- so anything reachable through here must be
    // hook-free, which the replication registry enforces at registration.
    bool SetComponentBytes(
        World& world,
        EntityId entity,
        ComponentTypeId type,
        std::span<const std::byte> bytes) const
    {
        assert(Sealed_ && "SetComponentBytes requires a sealed schema");
        const Entry* entry = Find(type);
        return entry != nullptr
            && entry->Write != nullptr
            && entry->Write(world, entity, bytes);
    }

    // Takes a component off an entity through its concrete type, so OnRemove
    // runs exactly as a typed RemoveComponent would. False when the entity does
    // not carry it, which a caller acting on someone else's account -- a
    // snapshot saying a component is gone -- treats as already true rather than
    // as a failure.
    //
    // Structural, like the typed call it wraps: not to be used inside a query.
    bool RemoveComponent(World& world, EntityId entity, ComponentTypeId type) const
    {
        assert(Sealed_ && "RemoveComponent requires a sealed schema");
        const Entry* entry = Find(type);
        return entry != nullptr
            && entry->Remove != nullptr
            && entry->Remove(world, entity);
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
    template <typename Owed, std::size_t... Index>
    static void CollectOwedIds(std::vector<ComponentTypeId>& out,
                               std::index_sequence<Index...>)
    {
        (out.push_back(ResolveComponentTypeId<std::tuple_element_t<Index, Owed>>()), ...);
    }

    template <typename T>
    static std::vector<ComponentTypeId> DeclaredOwedIds()
    {
        std::vector<ComponentTypeId> ids;
        if constexpr (ComponentOwesComponents<T>)
        {
            using Owed = typename ComponentTraits<T>::DerivedComponents;
            ids.reserve(std::tuple_size_v<Owed>);
            CollectOwedIds<Owed>(ids, std::make_index_sequence<std::tuple_size_v<Owed>>{});
        }
        return ids;
    }

    // Folds each component's declared set into everything it owes transitively.
    //
    // The typed add reaches the same set by recursion and stops on what is
    // already there, so a cycle terminates rather than hanging. It is still
    // refused here: a component that owes something that owes it back describes
    // two components that cannot be understood apart, which is a design to fix
    // rather than a shape to support. Refused at composition, once, where the
    // whole graph is visible.
    void ComputeOwedClosures()
    {
        for (Entry& entry : Entries_)
        {
            std::vector<ComponentTypeId> pending = entry.DeclaredOwed;
            entry.Owed.clear();
            while (!pending.empty())
            {
                const ComponentTypeId next = pending.back();
                pending.pop_back();

                assert(next != entry.Type
                       && "A component's DerivedComponents closure includes "
                          "itself: two components that cannot be understood "
                          "apart are one component.");
                if (next == entry.Type)
                    continue;

                const bool seen = std::find(entry.Owed.begin(), entry.Owed.end(), next)
                    != entry.Owed.end();
                if (seen)
                    continue;
                entry.Owed.push_back(next);

                if (const Entry* owed = Find(next))
                    pending.insert(pending.end(),
                                   owed->DeclaredOwed.begin(),
                                   owed->DeclaredOwed.end());
            }
            // Registration order, so two builds of the same schema produce the
            // same list and a signature built from it is the same signature.
            std::sort(entry.Owed.begin(), entry.Owed.end(),
                      [this](ComponentTypeId a, ComponentTypeId b)
                      { return IndexOf(a) < IndexOf(b); });
        }
    }

    [[nodiscard]] std::size_t IndexOf(ComponentTypeId type) const
    {
        for (std::size_t i = 0; i < Entries_.size(); ++i)
            if (Entries_[i].Type == type)
                return i;
        return Entries_.size();
    }

    std::vector<Entry> Entries_;
    bool Sealed_ = false;
};
