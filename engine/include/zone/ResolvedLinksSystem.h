#pragma once

class ConsoleRegistry;
class Logger;
class RuntimeWorld;
struct PreSimulateContext;

// Brings the World's ResolvedLinks up to date once a frame, before any tick,
// logs each link that turns invalid, answers the `links` console command and
// draws resolved links under `world.links.debug.draw`. docs/zone/links.md.
class ResolvedLinksSystem
{
public:
    ResolvedLinksSystem(RuntimeWorld& runtime, ConsoleRegistry* console, Logger* log);

    void PreSimulate(PreSimulateContext& ctx);

private:
    void Draw();

    RuntimeWorld& Runtime;
    ConsoleRegistry* Console = nullptr;
    Logger* Log = nullptr;
    bool Drawing = false;
};
