#pragma once

#include <script/ScriptVm.h>

#include <cstdint>

struct FixedLogicContext;
class World;
class LoggingProvider;

//=============================================================================
// ScriptSystemTick
//
// Runs T `system` declarations on the fixed tick. A `system` is not attached
// per entity: for each linked module's System declaration it iterates every
// entity whose archetype has the declaration's `over` components (the resolved
// signature) and invokes the `fixed` callback against a WorldScriptHost bound
// to that entity. One scheduled system, serial, chunk order; the reference
// determinism path.
//
// Self component-field writes are immediate; foreign and structural effects a
// callback requests go through a CommandBuffer flushed at the end of the tick.
//=============================================================================
class ScriptSystemTick
{
public:
    void FixedLogic(FixedLogicContext& ctx);

    // Runs one world's `system` declarations for a tick. Exposed so tests drive
    // it without a full schedule.
    void Step(World& world, std::uint64_t tickIndex, float dt);

    void SetLogging(LoggingProvider& logging) { Log = &logging; }

private:
    ScriptVm Vm;
    LoggingProvider* Log = nullptr;
};
