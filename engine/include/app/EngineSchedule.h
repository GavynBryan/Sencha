#pragma once

#include <app/GameContexts.h>

#include <cassert>
#include <queue>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

class ZoneRuntime;

template<typename T>
concept HasScheduleInit = requires(T& t) { t.Init(); };

template<typename T>
concept HasScheduleShutdown = requires(T& t) { t.Shutdown(); };

template<typename T>
concept HasFixedLogic = requires(T& t, FixedLogicContext& ctx) { t.FixedLogic(ctx); };

template<typename T>
concept HasPhysics = requires(T& t, PhysicsContext& ctx) { t.Physics(ctx); };

template<typename T>
concept HasPostFixed = requires(T& t, PostFixedContext& ctx) { t.PostFixed(ctx); };

template<typename T>
concept HasFrameUpdate = requires(T& t, FrameUpdateContext& ctx) { t.FrameUpdate(ctx); };

template<typename T>
concept HasExtractRender = requires(T& t, RenderExtractContext& ctx) { t.ExtractRender(ctx); };

template<typename T>
concept HasAudio = requires(T& t, AudioContext& ctx) { t.Audio(ctx); };

template<typename T>
concept HasEndFrame = requires(T& t, EndFrameContext& ctx) { t.EndFrame(ctx); };

template<typename T>
concept IsScheduledSystem =
    HasFixedLogic<T> || HasPhysics<T> || HasPostFixed<T> || HasFrameUpdate<T>
    || HasExtractRender<T> || HasAudio<T> || HasEndFrame<T>;

class EngineSchedule
{
public:
    EngineSchedule() = default;
    ~EngineSchedule() { Shutdown(); }

    EngineSchedule(const EngineSchedule&) = delete;
    EngineSchedule& operator=(const EngineSchedule&) = delete;
    EngineSchedule(EngineSchedule&&) = delete;
    EngineSchedule& operator=(EngineSchedule&&) = delete;

    template<typename T, typename... Args>
    T& Register(Args&&... args);

    template<typename T, typename TDep>
    void After();

    template<typename T>
    T* Get() const;

    template<typename T>
    bool Has() const;

    void Init();
    void Shutdown();

    FrameRegistryView BuildFrameView(ZoneRuntime& zones);

    void RunFixedLogic(FixedLogicContext& ctx);
    void RunPhysics(PhysicsContext& ctx);
    void RunPostFixed(PostFixedContext& ctx);
    void RunFrameUpdate(FrameUpdateContext& ctx);
    void RunExtractRender(RenderExtractContext& ctx);
    void RunAudio(AudioContext& ctx);
    void RunEndFrame(EndFrameContext& ctx);

private:
    template<typename TContext>
    struct DispatchEntry
    {
        std::type_index TypeId;
        void* Ptr = nullptr;
        void (*Fn)(void*, TContext&) = nullptr;
        std::vector<std::type_index> DependsOn;
    };

    struct SystemRecord
    {
        std::type_index TypeId{ typeid(void) };
        void* Ptr = nullptr;
        void (*DeleteFn)(void*) = nullptr;
        void (*InitFn)(void*) = nullptr;
        void (*ShutdownFn)(void*) = nullptr;
    };

    template<typename TContext>
    static void Run(const std::vector<DispatchEntry<TContext>>& entries, TContext& ctx);

    template<typename TContext>
    static void TopoSort(std::vector<DispatchEntry<TContext>>& entries);

    template<typename TContext>
    static void AddDependency(std::vector<DispatchEntry<TContext>>& entries,
                              std::type_index tid,
                              std::type_index dep);

    std::vector<SystemRecord> Records;
    std::unordered_map<std::type_index, void*> TypeIndex;

    std::vector<DispatchEntry<FixedLogicContext>> FixedLogicEntries;
    std::vector<DispatchEntry<PhysicsContext>> PhysicsEntries;
    std::vector<DispatchEntry<PostFixedContext>> PostFixedEntries;
    std::vector<DispatchEntry<FrameUpdateContext>> FrameUpdateEntries;
    std::vector<DispatchEntry<RenderExtractContext>> ExtractRenderEntries;
    std::vector<DispatchEntry<AudioContext>> AudioEntries;
    std::vector<DispatchEntry<EndFrameContext>> EndFrameEntries;

    bool Initialized = false;
};

template<typename T, typename... Args>
T& EngineSchedule::Register(Args&&... args)
{
    static_assert(IsScheduledSystem<T>,
        "T must implement at least one EngineSchedule phase method");

    assert(!Initialized && "Cannot register systems after EngineSchedule::Init()");
    assert(TypeIndex.find(std::type_index(typeid(T))) == TypeIndex.end()
        && "System type already registered");

    T* raw = new T(std::forward<Args>(args)...);
    TypeIndex.emplace(std::type_index(typeid(T)), static_cast<void*>(raw));

    SystemRecord rec;
    rec.TypeId = std::type_index(typeid(T));
    rec.Ptr = static_cast<void*>(raw);
    rec.DeleteFn = [](void* p) { delete static_cast<T*>(p); };
    if constexpr (HasScheduleInit<T>)
        rec.InitFn = [](void* p) { static_cast<T*>(p)->Init(); };
    if constexpr (HasScheduleShutdown<T>)
        rec.ShutdownFn = [](void* p) { static_cast<T*>(p)->Shutdown(); };
    Records.push_back(rec);

    if constexpr (HasFixedLogic<T>)
        FixedLogicEntries.push_back({ std::type_index(typeid(T)), raw,
            [](void* p, FixedLogicContext& ctx) { static_cast<T*>(p)->FixedLogic(ctx); }, {} });
    if constexpr (HasPhysics<T>)
        PhysicsEntries.push_back({ std::type_index(typeid(T)), raw,
            [](void* p, PhysicsContext& ctx) { static_cast<T*>(p)->Physics(ctx); }, {} });
    if constexpr (HasPostFixed<T>)
        PostFixedEntries.push_back({ std::type_index(typeid(T)), raw,
            [](void* p, PostFixedContext& ctx) { static_cast<T*>(p)->PostFixed(ctx); }, {} });
    if constexpr (HasFrameUpdate<T>)
        FrameUpdateEntries.push_back({ std::type_index(typeid(T)), raw,
            [](void* p, FrameUpdateContext& ctx) { static_cast<T*>(p)->FrameUpdate(ctx); }, {} });
    if constexpr (HasExtractRender<T>)
        ExtractRenderEntries.push_back({ std::type_index(typeid(T)), raw,
            [](void* p, RenderExtractContext& ctx) { static_cast<T*>(p)->ExtractRender(ctx); }, {} });
    if constexpr (HasAudio<T>)
        AudioEntries.push_back({ std::type_index(typeid(T)), raw,
            [](void* p, AudioContext& ctx) { static_cast<T*>(p)->Audio(ctx); }, {} });
    if constexpr (HasEndFrame<T>)
        EndFrameEntries.push_back({ std::type_index(typeid(T)), raw,
            [](void* p, EndFrameContext& ctx) { static_cast<T*>(p)->EndFrame(ctx); }, {} });

    return *raw;
}

template<typename T, typename TDep>
void EngineSchedule::After()
{
    const std::type_index tid(typeid(T));
    const std::type_index dep(typeid(TDep));
    AddDependency(FixedLogicEntries, tid, dep);
    AddDependency(PhysicsEntries, tid, dep);
    AddDependency(PostFixedEntries, tid, dep);
    AddDependency(FrameUpdateEntries, tid, dep);
    AddDependency(ExtractRenderEntries, tid, dep);
    AddDependency(AudioEntries, tid, dep);
    AddDependency(EndFrameEntries, tid, dep);
}

template<typename T>
T* EngineSchedule::Get() const
{
    auto it = TypeIndex.find(std::type_index(typeid(T)));
    return it != TypeIndex.end() ? static_cast<T*>(it->second) : nullptr;
}

template<typename T>
bool EngineSchedule::Has() const
{
    return TypeIndex.find(std::type_index(typeid(T))) != TypeIndex.end();
}

template<typename TContext>
void EngineSchedule::Run(const std::vector<DispatchEntry<TContext>>& entries, TContext& ctx)
{
    for (const auto& entry : entries)
        entry.Fn(entry.Ptr, ctx);
}

template<typename TContext>
void EngineSchedule::AddDependency(std::vector<DispatchEntry<TContext>>& entries,
                                   std::type_index tid,
                                   std::type_index dep)
{
    for (auto& entry : entries)
    {
        if (entry.TypeId == tid)
        {
            entry.DependsOn.push_back(dep);
            return;
        }
    }
}

template<typename TContext>
void EngineSchedule::TopoSort(std::vector<DispatchEntry<TContext>>& entries)
{
    if (entries.size() <= 1)
        return;

    std::unordered_map<std::type_index, size_t> idx;
    idx.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i)
        idx.emplace(entries[i].TypeId, i);

    std::vector<int> inDegree(entries.size(), 0);
    std::vector<std::vector<size_t>> adj(entries.size());

    for (size_t i = 0; i < entries.size(); ++i)
    {
        for (const auto& dep : entries[i].DependsOn)
        {
            auto it = idx.find(dep);
            if (it == idx.end())
                continue;
            const size_t j = it->second;
            adj[j].push_back(i);
            ++inDegree[i];
        }
    }

    std::queue<size_t> q;
    for (size_t i = 0; i < entries.size(); ++i)
        if (inDegree[i] == 0)
            q.push(i);

    std::vector<DispatchEntry<TContext>> sorted;
    sorted.reserve(entries.size());
    while (!q.empty())
    {
        const size_t n = q.front();
        q.pop();
        sorted.push_back(std::move(entries[n]));
        for (size_t m : adj[n])
            if (--inDegree[m] == 0)
                q.push(m);
    }

    assert(sorted.size() == entries.size()
        && "Cycle detected in EngineSchedule dependency declarations");

    entries = std::move(sorted);
}
