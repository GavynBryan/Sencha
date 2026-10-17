// Measurement probe for docs/plans/navigation-agent.md (D11). Copy into
// test/navigation/ to run; not built from the evidence folder.
#include "NavigationFixture.h"

#include <jobs/JobSystem.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    double Micros(Clock::time_point a, Clock::time_point b)
    {
        return std::chrono::duration<double, std::micro>(b - a).count();
    }

    double Median(std::vector<double> v)
    {
        if (v.empty())
            return 0.0;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    }

    NavBuildProfile TemplateProfile()
    {
        NavBuildProfile p;
        p.Radius = 0.3f;
        p.Height = 1.8f;
        p.MaxSlopeDegrees = 50.0f;
        p.MaxClimb = 0.35f;
        p.CellSize = 0.15f;
        p.CellHeight = 0.1f;
        p.TileCells = 32;
        return p;
    }

    NavTestGeometry Arena(float side, bool maze)
    {
        NavTestGeometry g;
        g.AddFloor(0.0f, 0.0f, side, side);
        for (float x = 4.0f; x < side - 1.0f; x += 4.0f)
            for (float z = 4.0f; z < side - 1.0f; z += 4.0f)
                g.AddBox(Vec3d(x - 0.5f, 0.0f, z - 0.5f), Vec3d(x + 0.5f, 3.0f, z + 0.5f));
        if (maze)
        {
            int row = 0;
            for (float z = 12.0f; z < side - 6.0f; z += 12.0f, ++row)
            {
                const float gap = (row % 2 == 0) ? side - 6.0f : 2.0f;
                if (gap > 4.0f)
                    g.AddBox(Vec3d(0.0f, 0.0f, z - 0.25f), Vec3d(gap, 3.0f, z + 0.25f));
                g.AddBox(Vec3d(gap + 4.0f, 0.0f, z - 0.25f), Vec3d(side, 3.0f, z + 0.25f));
            }
        }
        return g;
    }

    struct Lcg
    {
        std::uint64_t S = 0x9e3779b97f4a7c15ull;
        float Next()
        {
            S = S * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<float>((S >> 33) & 0xffffff) / static_cast<float>(0xffffff);
        }
    };

    // One planning request as the system would gather it: positions only.
    struct PlanInput
    {
        Vec3d From;
        Vec3d To;
    };

    struct PlanOutput
    {
        NavStatus Status = NavStatus::NoPath;
        std::size_t Nodes = 0;
        std::unique_ptr<NavRouteBuffer> Route;
    };

    struct Zone
    {
        GameplayTagRegistry Tags;
        GameplayTagId Humanoid;
        ZoneNavigation Navigation;
        NavQueryRequest Request;

        Zone(const NavTestGeometry& geometry)
        {
            Humanoid = *Tags.RegisterTag("navigation.profile.humanoid");
            NavigationSettings settings;
            settings.Profiles.push_back({ "navigation.profile.humanoid", TemplateProfile() });
            NavigationCookInput input;
            input.Positions = geometry.Positions;
            input.Indices = geometry.Indices;
            input.Settings = &settings;
            const NavigationCookResult cooked = CookZoneNavigation(input);
            EXPECT_TRUE(cooked.Navigation.has_value());
            EXPECT_TRUE(Navigation.Load(ZoneId{ 9 }, *cooked.Navigation, &Tags));
            Request.Zone = ZoneId{ 9 };
            Request.Profile = Humanoid;
        }
    };

    // The planning batch: per-worker contexts, one output per request, no
    // shared mutation. The same function runs inline or under ParallelFor.
    struct Planner
    {
        const Zone& Z;
        JobSystem& Jobs;
        std::vector<NavQueryContext> Contexts;

        Planner(const Zone& zone, JobSystem& jobs) : Z(zone), Jobs(jobs)
        {
            for (std::uint32_t i = 0; i <= jobs.WorkerCount(); ++i)
                Contexts.emplace_back(NavQueryContextConfig{ 4096, 512 });
        }

        void PlanOne(const PlanInput& in, PlanOutput& out, NavQueryContext& context) const
        {
            const NavProjectResult a = NavProjectPoint(Z.Navigation, context, Z.Request, in.From, Vec3d(1, 2, 1));
            const NavProjectResult b = NavProjectPoint(Z.Navigation, context, Z.Request, in.To, Vec3d(1, 2, 1));
            if (a.Status != NavStatus::Success || b.Status != NavStatus::Success)
            {
                out.Status = NavStatus::InvalidStart;
                out.Nodes = 0;
                return;
            }
            NavQueryDiagnostics diagnostics;
            out.Status = NavFindRoute(Z.Navigation, context, Z.Request, a.Location, b.Location, *out.Route,
                                      &diagnostics);
            out.Nodes = diagnostics.NodesVisited;
        }

        // Plans inputs[first, first + count). Below `minParallel` it stays on the caller.
        void Run(const std::vector<PlanInput>& inputs, std::vector<PlanOutput>& outputs, std::size_t first,
                 std::size_t count, std::size_t minParallel)
        {
            if (count < minParallel || Jobs.WorkerCount() == 0)
            {
                for (std::size_t i = 0; i < count; ++i)
                    PlanOne(inputs[first + i], outputs[first + i], Contexts[0]);
                return;
            }
            Jobs.ParallelFor(static_cast<std::uint32_t>(count), [&](std::uint32_t job) {
                PlanOne(inputs[first + job], outputs[first + job], Contexts[Jobs.CurrentWorkerIndex()]);
            });
        }
    };

    std::vector<PlanInput> RandomPairs(float side, std::size_t n, std::uint64_t seed)
    {
        Lcg rng;
        rng.S ^= seed;
        std::vector<PlanInput> out;
        while (out.size() < n)
        {
            const Vec3d a(2.0f + rng.Next() * (side - 4.0f), 0.0f, 2.0f + rng.Next() * (side - 4.0f));
            const Vec3d b(2.0f + rng.Next() * (side - 4.0f), 0.0f, 2.0f + rng.Next() * (side - 4.0f));
            out.push_back({ a, b });
        }
        return out;
    }

    // Everyone heads for one point: the shape of a repath storm after a door
    // or bridge on a shared route changes.
    std::vector<PlanInput> Converging(float side, std::size_t n, std::uint64_t seed)
    {
        std::vector<PlanInput> out = RandomPairs(side, n, seed);
        for (PlanInput& in : out)
            in.To = Vec3d(side * 0.5f + 0.1f, 0.0f, side - 3.0f);
        return out;
    }

    std::vector<PlanOutput> MakeOutputs(std::size_t n)
    {
        std::vector<PlanOutput> out(n);
        for (PlanOutput& o : out)
            o.Route = std::make_unique<NavRouteBuffer>(32, 64, 128);
        return out;
    }

    bool SameOutputs(const std::vector<PlanOutput>& a, const std::vector<PlanOutput>& b)
    {
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].Status != b[i].Status || a[i].Nodes != b[i].Nodes)
                return false;
            const auto ca = a[i].Route->Corners();
            const auto cb = b[i].Route->Corners();
            if (ca.size() != cb.size())
                return false;
            for (std::size_t k = 0; k < ca.size(); ++k)
                if (ca[k].X != cb[k].X || ca[k].Y != cb[k].Y || ca[k].Z != cb[k].Z)
                    return false;
        }
        return true;
    }

    const std::uint32_t kWorkerCounts[] = { 0, 1, 3, 7, 14 };

    void DispatchOverhead()
    {
        for (const std::uint32_t workers : kWorkerCounts)
        {
            if (workers == 0)
                continue;
            JobSystem jobs(workers);
            std::vector<double> samples;
            std::atomic<std::uint32_t> sink{ 0 };
            for (int r = 0; r < 2000; ++r)
            {
                const auto t0 = Clock::now();
                jobs.ParallelFor(workers + 1, [&](std::uint32_t i) { sink.fetch_add(i, std::memory_order_relaxed); });
                samples.push_back(Micros(t0, Clock::now()));
            }
            std::printf("[batch] dispatch overhead workers=%u: median %.2f us (empty batch of %u)\n", workers,
                        Median(samples), workers + 1);
        }
    }

    void BatchScaling(const char* label, const Zone& zone, const std::vector<PlanInput>& inputs)
    {
        std::vector<PlanOutput> reference;
        for (const std::uint32_t workers : kWorkerCounts)
        {
            JobSystem jobs(workers);
            Planner planner(zone, jobs);
            std::vector<PlanOutput> outputs = MakeOutputs(inputs.size());
            planner.Run(inputs, outputs, 0, 16, 1); // warm every context
            std::printf("[batch] %s workers=%u:", label, workers);
            for (const std::size_t size : { 1u, 2u, 4u, 8u, 16u, 32u, 64u, 256u })
            {
                std::vector<double> samples;
                const int reps = size >= 64 ? 15 : 60;
                for (int r = 0; r < reps; ++r)
                {
                    const std::size_t first = (static_cast<std::size_t>(r) * 37) % (inputs.size() - size + 1);
                    const auto t0 = Clock::now();
                    planner.Run(inputs, outputs, size == 256 ? 0 : first, size, 2);
                    samples.push_back(Micros(t0, Clock::now()));
                }
                std::printf(" n%zu=%.0fus", size, Median(samples));
            }
            planner.Run(inputs, outputs, 0, inputs.size(), 2);
            if (workers == 0)
                reference = std::move(outputs);
            else
                std::printf(" [%s serial]", SameOutputs(reference, outputs) ? "identical to" : "DIFFERS FROM");
            std::printf("\n");
        }
    }

    struct PolicyResult
    {
        int Ticks = 0;
        double MaxTickUs = 0.0;
        double MedianTickUs = 0.0;
        std::size_t MaxNodesTick = 0;
    };

    // Drains a storm of `inputs`, all enqueued at tick 0, under a per-tick
    // admission rule. `maxPlans` caps plans per tick; `nodeBudget` (0 = none)
    // stops admitting further waves once the tick's visited nodes reach it.
    // Waves are `wave` plans each; admission never depends on worker count.
    PolicyResult Drain(const Zone& zone, std::uint32_t workers, const std::vector<PlanInput>& inputs,
                       std::size_t maxPlans, std::size_t nodeBudget, std::size_t wave)
    {
        JobSystem jobs(workers);
        Planner planner(zone, jobs);
        std::vector<PlanOutput> outputs = MakeOutputs(inputs.size());
        planner.Run(inputs, outputs, 0, 16, 1);
        PolicyResult result;
        std::vector<double> ticks;
        std::size_t next = 0;
        while (next < inputs.size())
        {
            const auto t0 = Clock::now();
            std::size_t planned = 0;
            std::size_t nodes = 0;
            while (next < inputs.size() && planned < maxPlans && (nodeBudget == 0 || nodes < nodeBudget))
            {
                const std::size_t count = std::min({ wave, maxPlans - planned, inputs.size() - next });
                planner.Run(inputs, outputs, next, count, 2);
                for (std::size_t i = next; i < next + count; ++i)
                    nodes += outputs[i].Nodes;
                next += count;
                planned += count;
            }
            const double us = Micros(t0, Clock::now());
            ticks.push_back(us);
            result.MaxTickUs = std::max(result.MaxTickUs, us);
            result.MaxNodesTick = std::max(result.MaxNodesTick, nodes);
            ++result.Ticks;
        }
        result.MedianTickUs = Median(ticks);
        return result;
    }

    void Policies(const char* label, const Zone& zone, const std::vector<PlanInput>& inputs)
    {
        struct Policy
        {
            const char* Name;
            std::size_t MaxPlans;
            std::size_t NodeBudget;
            std::size_t Wave;
        };
        const Policy policies[] = {
            { "count 4", 4, 0, 4 },
            { "count 16", 16, 0, 16 },
            { "count 32", 32, 0, 32 },
            { "count 64", 64, 0, 64 },
            { "nodes 8k, waves 8, cap 64", 64, 8192, 8 },
            { "nodes 16k, waves 8, cap 64", 64, 16384, 8 },
            { "nodes 32k, waves 16, cap 128", 128, 32768, 16 },
        };
        for (const Policy& policy : policies)
            for (const std::uint32_t workers : { 0u, 3u, 7u, 14u })
            {
                std::vector<double> maxes, medians;
                PolicyResult last;
                for (int r = 0; r < 5; ++r)
                {
                    last = Drain(zone, workers, inputs, policy.MaxPlans, policy.NodeBudget, policy.Wave);
                    maxes.push_back(last.MaxTickUs);
                    medians.push_back(last.MedianTickUs);
                }
                std::printf("[policy] %s | %-28s workers=%2u: ticks %3d, tick median %.0f us, tick max %.0f us, max nodes/tick %zu\n",
                            label, policy.Name, workers, last.Ticks, Median(medians), Median(maxes), last.MaxNodesTick);
            }
    }
}

TEST(NavPlanningBatchProbe, ContextMemory)
{
    const Zone maze(Arena(96.0f, true));
    const std::vector<PlanInput> pairs = RandomPairs(96.0f, 15, 5);
    const auto rss = []
    {
        long pages = 0, resident = 0;
        std::FILE* f = std::fopen("/proc/self/statm", "r");
        if (f != nullptr)
        {
            (void)std::fscanf(f, "%ld %ld", &pages, &resident);
            std::fclose(f);
        }
        return resident * 4096L;
    };
    for (const std::uint32_t maxNodes : { 2048u, 4096u })
    {
        const long before = rss();
        std::vector<NavQueryContext> contexts;
        for (int i = 0; i < 15; ++i)
            contexts.emplace_back(NavQueryContextConfig{ maxNodes, 512 });
        std::vector<PlanOutput> outputs = MakeOutputs(15);
        JobSystem jobs(0);
        Planner planner(maze, jobs);
        for (int i = 0; i < 15; ++i)
            planner.PlanOne(pairs[static_cast<std::size_t>(i)], outputs[static_cast<std::size_t>(i)], contexts[static_cast<std::size_t>(i)]);
        const long after = rss();
        std::printf("[memory] maxNodes=%u: 15 warmed contexts + 1 planner context + 15 route buffers: %.1f KB resident growth\n",
                    maxNodes, static_cast<double>(after - before) / 1024.0);
    }
}

TEST(NavPlanningBatchProbe, Measure)
{
    DispatchOverhead();
    const Zone pillars(Arena(96.0f, false));
    const Zone maze(Arena(96.0f, true));
    const std::vector<PlanInput> pillarPairs = RandomPairs(96.0f, 256, 1);
    const std::vector<PlanInput> mazePairs = RandomPairs(96.0f, 256, 2);
    const std::vector<PlanInput> mazeConverging = Converging(96.0f, 256, 3);
    BatchScaling("pillars random", pillars, pillarPairs);
    BatchScaling("maze random", maze, mazePairs);
    BatchScaling("maze converging", maze, mazeConverging);
    Policies("maze random", maze, mazePairs);
    Policies("maze converging", maze, mazeConverging);
    Policies("pillars random", pillars, pillarPairs);
}
