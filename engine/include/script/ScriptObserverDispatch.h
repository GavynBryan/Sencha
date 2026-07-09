#pragma once

#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>
#include <script/ScriptVm.h>

#include <cstdint>
#include <vector>

class World;
class LoggingProvider;

//=============================================================================
// ScriptObserverDispatch
//
// A world resource that runs T `observer` bodies at component transitions. For
// each observed component it installs one World TransitionDispatch; when that
// component is added or removed, the dispatch runs every registered observer's
// on_add / on_remove callback through the VM, against a WorldScriptHost bound to
// the world's ACTIVE reaction scope, so the observer's deferred effects land in
// the next reaction wave. Observers read live committed state and defer all
// effects (enforced at compile time); this bridge never mutates immediately.
//=============================================================================
class ScriptObserverDispatch
{
public:
    struct Binding
    {
        std::uint32_t LinkedModule = 0;
        std::uint32_t Declaration = 0;
        std::int32_t OnAddFn = -1;    // function index, or -1
        std::int32_t OnRemoveFn = -1; // function index, or -1
    };

    // Registers an observer on `component`. Installs the World transition
    // dispatch the first time a component is observed.
    void Register(World& world, ComponentId component, const Binding& binding);

    void SetLogging(LoggingProvider& logging) { Log = &logging; }

private:
    static void OnAddCb(World&, EntityId, ComponentId, const void* component, void* context);
    static void OnRemoveCb(World&, EntityId, ComponentId, const void* component, void* context);
    void Fire(World& world, EntityId entity, ComponentId component, bool isAdd);

    ScriptVm Vm;
    // Indexed by ComponentId (ids are small, < 256).
    std::vector<std::vector<Binding>> ByComponent;
    LoggingProvider* Log = nullptr;
};
