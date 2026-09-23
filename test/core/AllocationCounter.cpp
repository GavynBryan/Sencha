#include "AllocationCounter.h"

#include <cstdlib>
#include <new>

namespace
{
std::size_t gAllocations = 0;
}

std::size_t AllocationCount()
{
    return gAllocations;
}

void* operator new(std::size_t size)
{
    ++gAllocations;
    if (void* memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}
