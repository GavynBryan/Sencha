#include <authored/AuthoredCatalog.h>

#include <atomic>

std::uint64_t NextAuthoredCatalogNumber()
{
    static std::atomic<std::uint64_t> counter{ 0 };
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}
