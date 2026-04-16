#pragma once

#include <core/system/SystemConcepts.h>
#include <cassert>
#include <queue>
#include <typeindex>
#include <unordered_map>
#include <vector>

//=============================================================================
// SystemHost
//
// Owns system instances and dispatches them across three fixed lanes:
//
//   Frame  — runs once per frame.    Systems implement Update(float dt).
//   Fixed  — runs 0..N times/frame.  Systems implement Tick(float fixedDt).
//   Render — runs once per frame.    Systems implement Render(float alpha).
//
// A system participates in every lane for which it implements the matching
// method. Capability is checked via concepts at Register() — no base class,
// no vtable, dispatch is via erased function pointers.
//
// Ordering within a lane is declared with After<T, TDep>():
//   host.After<KinematicMoveSystem2D, ColliderSyncSystem2D>();
// means T runs after TDep in any lane they share. Dependencies referencing a
// type not present in a given lane are silently ignored.
//
// All After() calls must precede Init(). Init() resolves each lane's order via
// topological sort (Kahn's algorithm) and asserts on cycles.
//
// Usage:
//   SystemHost host;
//   host.Register<SdlInputSystem>(logging, bindings);
//   host.Register<PlayerMotorSystem2D>(...);
//   host.After<PlayerMotorSystem2D, SdlInputSystem>();
//   host.Init();
//
//   // per-frame main loop:
//   host.RunFrame(frameClock.Dt);
//   // fixed accumulator loop (0..N):
//   host.RunFixed(sim.FixedDt);
//   // render:
//   host.RunRender(alpha);
//
//   host.Shutdown();
//=============================================================================
class SystemHost
{
public:
    // Register a system. T must satisfy IsSystem (at least one of Update/Tick/Render).
    // Must be called before Init().
    template<typename T, typename... Args>
    T& Register(Args&&... args);

    // Declare that T must run after TDep in any lane they share.
    // Must be called before Init().
    template<typename T, typename TDep>
    void After();

    // Look up a registered system by concrete type. Returns nullptr if not found.
    template<typename T>
    T* Get() const;

    template<typename T>
    bool Has() const;

    // Topologically sort each lane's dispatch order, then call Init() on all systems.
    void Init();

    void RunFrame(float dt);       // Dispatches HasUpdate systems
    void RunFixed(float fixedDt);  // Dispatches HasTick systems
    void RunRender(float alpha);   // Dispatches HasRender systems

    // Calls Shutdown() on all systems in reverse registration order, then frees storage.
    void Shutdown();

private:
    struct DispatchEntry
    {
        std::type_index              TypeId;
        void*                        Ptr;
        void                       (*Fn)(void*, float);
        std::vector<std::type_index> DependsOn;
    };

    struct SystemRecord
    {
        std::type_index TypeId { typeid(void) };
        void*           Ptr          = nullptr;
        void          (*DeleteFn  )(void*) = nullptr;
        void          (*InitFn    )(void*) = nullptr;
        void          (*ShutdownFn)(void*) = nullptr;
    };

    std::vector<SystemRecord>                  Records;
    std::unordered_map<std::type_index, void*> TypeIndex;
    std::vector<DispatchEntry>                 FrameEntries;
    std::vector<DispatchEntry>                 FixedEntries;
    std::vector<DispatchEntry>                 RenderEntries;
    bool                                       Initialized = false;

    static void TopoSort(std::vector<DispatchEntry>& entries);
    static void Run(const std::vector<DispatchEntry>& entries, float param);
};

// ---------------------------------------------------------------------------
// Template implementations
// ---------------------------------------------------------------------------

template<typename T, typename... Args>
T& SystemHost::Register(Args&&... args)
{
    static_assert(IsSystem<T>,
        "T must implement at least one of: Update(float), Tick(float), Render(float)");

    assert(!Initialized &&
        "Cannot register systems after Init() — call Register() during setup");
    assert(TypeIndex.find(std::type_index(typeid(T))) == TypeIndex.end() &&
        "System type already registered");

    T* raw = new T(std::forward<Args>(args)...);
    TypeIndex.emplace(std::type_index(typeid(T)), static_cast<void*>(raw));

    SystemRecord rec;
    rec.TypeId   = std::type_index(typeid(T));
    rec.Ptr      = static_cast<void*>(raw);
    rec.DeleteFn = [](void* p){ delete static_cast<T*>(p); };
    if constexpr (HasInit<T>)
        rec.InitFn = [](void* p){ static_cast<T*>(p)->Init(); };
    if constexpr (HasShutdown<T>)
        rec.ShutdownFn = [](void* p){ static_cast<T*>(p)->Shutdown(); };
    Records.push_back(rec);

    if constexpr (HasUpdate<T>)
        FrameEntries.push_back({
            std::type_index(typeid(T)), raw,
            [](void* p, float dt){ static_cast<T*>(p)->Update(dt); }, {}});
    if constexpr (HasTick<T>)
        FixedEntries.push_back({
            std::type_index(typeid(T)), raw,
            [](void* p, float dt){ static_cast<T*>(p)->Tick(dt); }, {}});
    if constexpr (HasRender<T>)
        RenderEntries.push_back({
            std::type_index(typeid(T)), raw,
            [](void* p, float alpha){ static_cast<T*>(p)->Render(alpha); }, {}});

    return *raw;
}

template<typename T, typename TDep>
void SystemHost::After()
{
    auto addDep = [](std::vector<DispatchEntry>& entries,
                     std::type_index tid, std::type_index dep)
    {
        for (auto& e : entries)
        {
            if (e.TypeId == tid)
            {
                e.DependsOn.push_back(dep);
                return;
            }
        }
    };
    const std::type_index tid(typeid(T));
    const std::type_index dep(typeid(TDep));
    addDep(FrameEntries,  tid, dep);
    addDep(FixedEntries,  tid, dep);
    addDep(RenderEntries, tid, dep);
}

template<typename T>
T* SystemHost::Get() const
{
    auto it = TypeIndex.find(std::type_index(typeid(T)));
    return it != TypeIndex.end() ? static_cast<T*>(it->second) : nullptr;
}

template<typename T>
bool SystemHost::Has() const
{
    return TypeIndex.find(std::type_index(typeid(T))) != TypeIndex.end();
}

inline void SystemHost::Init()
{
    TopoSort(FrameEntries);
    TopoSort(FixedEntries);
    TopoSort(RenderEntries);

    for (auto& rec : Records)
        if (rec.InitFn) rec.InitFn(rec.Ptr);

    Initialized = true;
}

inline void SystemHost::RunFrame(float dt)    { Run(FrameEntries,  dt); }
inline void SystemHost::RunFixed(float dt)    { Run(FixedEntries,  dt); }
inline void SystemHost::RunRender(float alpha){ Run(RenderEntries, alpha); }

inline void SystemHost::Shutdown()
{
    for (auto it = Records.rbegin(); it != Records.rend(); ++it)
        if (it->ShutdownFn) it->ShutdownFn(it->Ptr);

    for (auto it = Records.rbegin(); it != Records.rend(); ++it)
        if (it->DeleteFn) it->DeleteFn(it->Ptr);

    Records.clear();
    TypeIndex.clear();
    FrameEntries.clear();
    FixedEntries.clear();
    RenderEntries.clear();
    Initialized = false;
}

inline void SystemHost::Run(const std::vector<DispatchEntry>& entries, float param)
{
    for (const auto& e : entries)
        e.Fn(e.Ptr, param);
}

inline void SystemHost::TopoSort(std::vector<DispatchEntry>& entries)
{
    if (entries.size() <= 1) return;

    std::unordered_map<std::type_index, size_t> idx;
    idx.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i)
        idx.emplace(entries[i].TypeId, i);

    std::vector<int>                 inDegree(entries.size(), 0);
    std::vector<std::vector<size_t>> adj(entries.size());

    for (size_t i = 0; i < entries.size(); ++i)
    {
        for (const auto& dep : entries[i].DependsOn)
        {
            auto it = idx.find(dep);
            if (it == idx.end()) continue; // dep not in this lane, ignore
            size_t j = it->second;         // i runs after j → edge j→i
            adj[j].push_back(i);
            ++inDegree[i];
        }
    }

    std::queue<size_t> q;
    for (size_t i = 0; i < entries.size(); ++i)
        if (inDegree[i] == 0) q.push(i);

    std::vector<DispatchEntry> sorted;
    sorted.reserve(entries.size());
    while (!q.empty())
    {
        size_t n = q.front(); q.pop();
        sorted.push_back(std::move(entries[n]));
        for (size_t m : adj[n])
            if (--inDegree[m] == 0) q.push(m);
    }

    assert(sorted.size() == entries.size() &&
           "Cycle detected in system dependency declarations");

    entries = std::move(sorted);
}
