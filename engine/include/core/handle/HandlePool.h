#pragma once

#include <core/handle/Handle.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

// Owns values addressed by Handle<Tag>. Every lookup checks the generation, so
// a handle kept past its value's erase never reaches whatever reuses the slot.
// Values are heap-held, so they need not be movable and never relocate.
// Iteration runs in slot order, which is deterministic for a given history.
template <typename Tag, typename T>
class HandlePool
{
public:
    using HandleType = Handle<Tag>;

    template <typename... Args>
    HandleType Emplace(Args&&... args)
    {
        return Adopt(std::make_unique<T>(std::forward<Args>(args)...));
    }

    // Takes ownership of a value built elsewhere; null yields a null handle.
    HandleType Adopt(std::unique_ptr<T> value)
    {
        if (value == nullptr)
            return {};
        std::uint32_t index = 0;
        if (!FreeSlots.empty())
        {
            index = FreeSlots.back();
            FreeSlots.pop_back();
        }
        else
        {
            index = static_cast<std::uint32_t>(Slots.size());
            Slots.emplace_back();
        }
        Slot& slot = Slots[index];
        ++slot.Generation;
        slot.Value = std::move(value);
        ++Count;
        return HandleType{ index, slot.Generation };
    }

    // Destroys the value; false for a null or stale handle.
    bool Erase(HandleType handle)
    {
        Slot* slot = Resolve(handle);
        if (slot == nullptr)
            return false;
        slot->Value.reset();
        FreeSlots.push_back(handle.Index);
        --Count;
        return true;
    }

    // Hands the value out of the pool, leaving the handle stale.
    std::unique_ptr<T> Release(HandleType handle)
    {
        Slot* slot = Resolve(handle);
        if (slot == nullptr)
            return nullptr;
        std::unique_ptr<T> value = std::move(slot->Value);
        FreeSlots.push_back(handle.Index);
        --Count;
        return value;
    }

    [[nodiscard]] T* Find(HandleType handle)
    {
        Slot* slot = Resolve(handle);
        return slot != nullptr ? slot->Value.get() : nullptr;
    }

    [[nodiscard]] const T* Find(HandleType handle) const
    {
        return const_cast<HandlePool*>(this)->Find(handle);
    }

    [[nodiscard]] bool Contains(HandleType handle) const { return Find(handle) != nullptr; }
    [[nodiscard]] std::size_t Size() const { return Count; }

    template <typename Fn>
    void ForEach(Fn&& fn)
    {
        for (std::uint32_t i = 1; i < Slots.size(); ++i)
            if (Slots[i].Value != nullptr)
                fn(HandleType{ i, Slots[i].Generation }, *Slots[i].Value);
    }

    template <typename Fn>
    void ForEach(Fn&& fn) const
    {
        for (std::uint32_t i = 1; i < Slots.size(); ++i)
            if (Slots[i].Value != nullptr)
                fn(HandleType{ i, Slots[i].Generation }, static_cast<const T&>(*Slots[i].Value));
    }

    // Generations survive, so no handle issued before this resolves after it.
    void Clear()
    {
        FreeSlots.clear();
        for (std::size_t i = Slots.size() - 1; i >= 1; --i)
        {
            Slots[i].Value.reset();
            FreeSlots.push_back(static_cast<std::uint32_t>(i));
        }
        Count = 0;
    }

private:
    struct Slot
    {
        std::unique_ptr<T> Value;
        std::uint32_t Generation = 0;
    };

    [[nodiscard]] Slot* Resolve(HandleType handle)
    {
        if (!handle.IsValid() || handle.Index >= Slots.size())
            return nullptr;
        Slot& slot = Slots[handle.Index];
        return slot.Value != nullptr && slot.Generation == handle.Generation ? &slot : nullptr;
    }

    // Index 0 is the reserved null slot.
    std::vector<Slot> Slots = std::vector<Slot>(1);
    std::vector<std::uint32_t> FreeSlots;
    std::size_t Count = 0;
};
