#include "viewport/ViewClock.h"

#include <chrono>

double EditorViewSeconds()
{
    static const auto started = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}
