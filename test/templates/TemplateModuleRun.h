#pragma once

#include <app/EngineSchedule.h>
#include <app/GameModuleRun.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

//=============================================================================
// `Probe` is a system the test registers on the engine before the run. It sees
// the world every frame, which is the only honest place to observe a template's
// composition: the game's own teardown runs before Run returns.
//=============================================================================
template <typename Probe>
class TemplateModuleRun
{
public:
    TemplateModuleRun(std::string_view modulePath, std::string_view contentRoot, std::string_view map, int frames)
        : Runner(GameModuleRunSettings{ .Module = std::string(modulePath),
                                        .ContentRoot = std::string(contentRoot),
                                        .Map = std::string(map),
                                        .Frames = static_cast<std::uint64_t>(frames),
                                        .TargetFps = 1000.0,
                                        .Adjust = [](EngineConfig& config) { config.Debug.ConsoleLogging = false; } })
    {
        if (!Runner.Loaded())
        {
            ADD_FAILURE() << "module did not load: " << Runner.Error();
            return;
        }
        Observed = &Runner.Host().Schedule().Register<Probe>();
        if constexpr (requires { Observed->Host = &Runner.Host(); })
            Observed->Host = &Runner.Host();
        ExitCode = Runner.Run();
    }

    [[nodiscard]] bool Loaded() const { return Runner.Loaded(); }
    [[nodiscard]] int Exit() const { return ExitCode; }
    [[nodiscard]] const Probe& Seen() const { return *Observed; }

private:
    GameModuleRun Runner;
    Probe* Observed = nullptr;
    int ExitCode = -1;
};
