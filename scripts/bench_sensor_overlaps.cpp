#include <physics/SensorOverlaps.h>

#include <chrono>
#include <iostream>

int main()
{
    for (const std::uint32_t count : { 100u, 1000u, 4096u })
    {
        SensorOverlaps overlaps(count);
        const auto start = std::chrono::steady_clock::now();
        for (std::uint64_t tick = 0; tick < 200; ++tick)
        {
            overlaps.Begin();
            for (std::uint32_t i = count; i != 0; --i)
                if (!overlaps.Observe({ { i, 1 }, { count + 1, 1 } }, true))
                    return 1;
            if (!overlaps.Commit(tick))
                return 2;
        }
        const double microseconds = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - start).count() / 200;
        std::cout << count << " pairs: " << microseconds << " us/sample\n";
    }
}
