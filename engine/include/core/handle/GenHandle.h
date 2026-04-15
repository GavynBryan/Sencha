#pragma once

#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

//=============================================================================
// GenHandle<T>
//
// Typed generation handle: (Index, Generation) stored as two uint32_ts.
// T is a compile-time tag that prevents mixing handles of different types.
// Trivially copyable; no heap allocation.
//=============================================================================
template <typename T>
struct GenHandle
{
    uint32_t Index      = ~0u;
    uint32_t Generation = 0;

    [[nodiscard]] bool IsNull() const { return Index == ~0u; }
};

//=============================================================================
// SlotRegistry
//
// Heap-allocated slot table (always held via shared_ptr). Observers (GenRef)
// hold a weak_ptr to it. When the owning context releases its shared_ptr --
// either at the end of ~Renderer or by an explicit reset -- all weak_ptr::lock
// calls return nullptr and Get() returns nullptr without touching freed memory.
//
// RemoveAll() should be called before the owner begins tearing down its
// pointees. It vacates every slot and bumps generations, so Get() returns
// nullptr even while the weak_ptr is still lockable. This provides two-stage
// protection: stale before the shared_ptr drops, null after it drops.
//
// Not thread-safe. Assumes single-threaded renderer ownership.
//=============================================================================
class SlotRegistry
{
public:
    // O(1) via free list (O(n) only on first insert into a fresh registry).
    // Asserts ptr != nullptr.
    template <typename T>
    [[nodiscard]] GenHandle<T> Insert(T* ptr)
    {
        assert(ptr != nullptr);

        if (!FreeList.empty())
        {
            const uint32_t index = FreeList.back();
            FreeList.pop_back();
            assert(Slots[index].Ptr == nullptr);
            Slots[index].Ptr = ptr;
            return {index, Slots[index].Generation};
        }

        const uint32_t index = static_cast<uint32_t>(Slots.size());
        Slots.push_back({ptr, 0u});
        return {index, 0u};
    }

    // Vacate one slot and bump its generation. Asserts the slot is occupied --
    // double-Remove on the same index is a bug, not a no-op.
    void Remove(uint32_t index)
    {
        assert(index < static_cast<uint32_t>(Slots.size()));
        assert(Slots[index].Ptr != nullptr && "SlotRegistry::Remove: slot is already empty");
        Slots[index].Ptr = nullptr;
        ++Slots[index].Generation;
        FreeList.push_back(index);
    }

    // Vacate every slot, bump all generations, rebuild the free list.
    // Call this before tearing down any pointees so outstanding GenRefs
    // see nullptr immediately, even while the shared_ptr is still alive.
    void RemoveAll()
    {
        FreeList.clear();
        FreeList.reserve(Slots.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(Slots.size()); ++i)
        {
            if (Slots[i].Ptr != nullptr)
            {
                Slots[i].Ptr = nullptr;
                ++Slots[i].Generation;
            }
            FreeList.push_back(i);
        }
    }

    // O(1): bounds check + null check + generation match + cast.
    // Safe: Insert<T> stored a T* as void*; this round-trip is well-defined
    // for the exact T used at insert time.
    template <typename T>
    [[nodiscard]] T* Resolve(GenHandle<T> handle) const
    {
        if (handle.Index >= static_cast<uint32_t>(Slots.size()))
            return nullptr;
        const Slot& slot = Slots[handle.Index];
        if (slot.Ptr == nullptr || slot.Generation != handle.Generation)
            return nullptr;
        return static_cast<T*>(slot.Ptr);
    }

private:
    struct Slot
    {
        void*    Ptr        = nullptr;
        uint32_t Generation = 0;
    };

    std::vector<Slot>     Slots;
    std::vector<uint32_t> FreeList;
};

//=============================================================================
// GenRef<T>
//
// Non-owning resolver. Stores a weak_ptr<SlotRegistry> and a GenHandle<T>.
//
// Get() locks the weak_ptr first. If the registry has been destroyed (the
// owning Renderer went away), lock() returns nullptr and Get() returns nullptr
// without touching any freed memory. If the lock succeeds but the slot has
// been vacated or the generation doesn't match, Resolve() returns nullptr.
//
// Two-stage staleness:
//   - Slot vacated (RemoveAll before teardown) -> Resolve returns nullptr
//   - Registry destroyed (shared_ptr dropped)  -> lock() returns nullptr
//=============================================================================
template <typename T>
class GenRef
{
public:
    GenRef() = default;

    // Takes a shared_ptr; stores it as weak_ptr so GenRef is non-owning.
    GenRef(std::shared_ptr<SlotRegistry> registry, GenHandle<T> handle)
        : Registry(std::move(registry)), Handle(handle) {}

    // O(1) resolve (plus weak_ptr lock overhead). Returns nullptr if stale
    // or if the registry has been destroyed.
    [[nodiscard]] T* Get() const
    {
        const auto reg = Registry.lock();
        return reg ? reg->Resolve(Handle) : nullptr;
    }

    [[nodiscard]] bool IsValid() const { return Get() != nullptr; }
    explicit operator bool() const { return IsValid(); }

    T* operator->() const
    {
        T* ptr = Get();
        assert(ptr != nullptr && "GenRef: accessed stale handle");
        return ptr;
    }

    T& operator*() const
    {
        T* ptr = Get();
        assert(ptr != nullptr && "GenRef: accessed stale handle");
        return *ptr;
    }

private:
    std::weak_ptr<SlotRegistry> Registry;
    GenHandle<T>                Handle = {};
};
