#include "AllocationCounter.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace
{
    std::atomic<std::size_t> gAllocations{ 0 };

    void* Allocate(std::size_t size)
    {
        gAllocations.fetch_add(1, std::memory_order_relaxed);
        return std::malloc(size == 0 ? 1 : size);
    }

    void* AllocateAligned(std::size_t size, std::align_val_t alignment)
    {
        gAllocations.fetch_add(1, std::memory_order_relaxed);
        const std::size_t align = static_cast<std::size_t>(alignment);
        const std::size_t rounded = ((size == 0 ? 1 : size) + align - 1) / align * align;
        return std::aligned_alloc(align, rounded);
    }

    template <typename Result>
    Result Checked(Result memory)
    {
        if (memory == nullptr)
            throw std::bad_alloc();
        return memory;
    }
}

std::size_t AllocationCount()
{
    return gAllocations.load(std::memory_order_relaxed);
}

void* operator new(std::size_t size) { return Checked(Allocate(size)); }
void* operator new[](std::size_t size) { return Checked(Allocate(size)); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return Allocate(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return Allocate(size); }
void* operator new(std::size_t size, std::align_val_t alignment) { return Checked(AllocateAligned(size, alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return Checked(AllocateAligned(size, alignment)); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return AllocateAligned(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return AllocateAligned(size, alignment);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept { std::free(memory); }
