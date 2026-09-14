#include <platform/ProcessMemory.h>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <unistd.h>

#include <cstdio>
#endif

std::uint64_t ProcessResidentBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return 0;
    return static_cast<std::uint64_t>(counters.WorkingSetSize);
#elif defined(__linux__)
    // statm's second field is the resident page count.
    FILE* statm = std::fopen("/proc/self/statm", "r");
    if (statm == nullptr)
        return 0;
    unsigned long size = 0;
    unsigned long resident = 0;
    const int read = std::fscanf(statm, "%lu %lu", &size, &resident);
    std::fclose(statm);
    if (read != 2)
        return 0;
    return static_cast<std::uint64_t>(resident) * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#else
    return 0;
#endif
}
