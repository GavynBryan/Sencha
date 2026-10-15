#include "identity/RandomKey.h"

#include <format>

std::string MintStableKey()
{
    thread_local RandomKeySource source;
    std::uint64_t value = 0;
    while (value == 0)
        value = source.Next();
    return std::format("{:016x}", value);
}
