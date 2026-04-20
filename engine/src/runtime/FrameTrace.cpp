#include <runtime/FrameTrace.h>

#include <cstdio>
#include <fstream>

uint64_t ChromeJsonFrameTrace::NowMicros() const
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - Epoch).count());
}

void ChromeJsonFrameTrace::BeginFrame(uint64_t frameIndex)
{
    Events.push_back(Event{ "Frame " + std::to_string(frameIndex), 'B', NowMicros() });
}

void ChromeJsonFrameTrace::EndFrame(uint64_t frameIndex)
{
    Events.push_back(Event{ "Frame " + std::to_string(frameIndex), 'E', NowMicros() });
}

void ChromeJsonFrameTrace::BeginPhase(const char* name)
{
    Events.push_back(Event{ name, 'B', NowMicros() });
}

void ChromeJsonFrameTrace::EndPhase(const char* name)
{
    Events.push_back(Event{ name, 'E', NowMicros() });
}

void ChromeJsonFrameTrace::Mark(const char* name)
{
    Events.push_back(Event{ name, 'i', NowMicros() });
}

bool ChromeJsonFrameTrace::WriteTo(const std::string& path) const
{
    std::ofstream out(path);
    if (!out) return false;

    out << "[\n";
    bool first = true;
    for (const auto& event : Events)
    {
        if (!first) out << ",\n";
        first = false;

        out << "  {\"name\":\"";
        for (char c : event.Name)
        {
            if (c == '"' || c == '\\') out << '\\';
            out << c;
        }
        out << "\",\"ph\":\"" << event.Phase
            << "\",\"pid\":1,\"tid\":1,\"ts\":" << event.Micros << '}';
    }
    out << "\n]\n";
    return out.good();
}
