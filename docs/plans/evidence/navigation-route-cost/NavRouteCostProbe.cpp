// Measurement probe for docs/plans/navigation-agent.md. Copy into test/navigation/ to run; not built from here.
#include "NavigationFixture.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

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

    // side x side metre floor with 1 m pillars every `spacing` metres and, when
    // `maze`, long walls with staggered gaps every 12 m.
    NavTestGeometry Arena(float side, float spacing, bool maze)
    {
        NavTestGeometry g;
        g.AddFloor(0.0f, 0.0f, side, side);
        for (float x = spacing; x < side - 1.0f; x += spacing)
            for (float z = spacing; z < side - 1.0f; z += spacing)
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

    struct Stats
    {
        double Median = 0, P90 = 0, Max = 0, Mean = 0;
    };

    Stats Summarize(std::vector<double> us)
    {
        Stats s;
        if (us.empty())
            return s;
        std::sort(us.begin(), us.end());
        s.Median = us[us.size() / 2];
        s.P90 = us[std::min(us.size() - 1, us.size() * 9 / 10)];
        s.Max = us.back();
        double total = 0;
        for (double v : us)
            total += v;
        s.Mean = total / static_cast<double>(us.size());
        return s;
    }

    void Probe(const char* label, const NavTestGeometry& geometry, float side, float minDistance,
               std::uint32_t maxNodes)
    {
        GameplayTagRegistry tags;
        const GameplayTagId humanoid = *tags.RegisterTag("navigation.profile.humanoid");
        NavigationSettings settings;
        settings.Profiles.push_back({ "navigation.profile.humanoid", TemplateProfile() });
        NavigationCookInput input;
        input.Positions = geometry.Positions;
        input.Indices = geometry.Indices;
        input.Settings = &settings;
        const NavigationCookResult cooked = CookZoneNavigation(input);
        ASSERT_TRUE(cooked.Navigation.has_value());
        ZoneNavigation zone;
        ASSERT_TRUE(zone.Load(ZoneId{ 7 }, *cooked.Navigation, &tags));

        NavQueryRequest request;
        request.Zone = ZoneId{ 7 };
        request.Profile = humanoid;
        NavQueryContext context(NavQueryContextConfig{ maxNodes, 512 });
        NavRouteBuffer route(32, 256, 256);

        std::vector<NavLocation> starts;
        std::vector<NavLocation> ends;
        Lcg rng;
        std::vector<double> projectUs;
        while (starts.size() < 512)
        {
            const Vec3d a(1.0f + rng.Next() * (side - 2.0f), 0.0f, 1.0f + rng.Next() * (side - 2.0f));
            const Vec3d b(1.0f + rng.Next() * (side - 2.0f), 0.0f, 1.0f + rng.Next() * (side - 2.0f));
            if ((a - b).Magnitude() < minDistance)
                continue;
            const auto t0 = Clock::now();
            const NavProjectResult pa = NavProjectPoint(zone, context, request, a, Vec3d(1, 2, 1));
            const auto t1 = Clock::now();
            const NavProjectResult pb = NavProjectPoint(zone, context, request, b, Vec3d(1, 2, 1));
            projectUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            if (pa.Status != NavStatus::Success || pb.Status != NavStatus::Success)
                continue;
            starts.push_back(pa.Location);
            ends.push_back(pb.Location);
        }

        // Warm-up.
        for (std::size_t i = 0; i < 16; ++i)
            (void)NavFindRoute(zone, context, request, starts[i], ends[i], route);

        std::vector<double> findUs, validateUs, raycastUs;
        std::size_t ok = 0, limit = 0, other = 0, corners = 0, deps = 0, nodes = 0, maxCorners = 0, maxDeps = 0, maxNodes2 = 0, cap = 0;
        for (std::size_t i = 0; i < starts.size(); ++i)
        {
            NavQueryDiagnostics diagnostics;
            const auto t0 = Clock::now();
            const NavStatus status =
                NavFindRoute(zone, context, request, starts[i], ends[i], route, &diagnostics);
            const auto t1 = Clock::now();
            findUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            nodes += diagnostics.NodesVisited;
            maxNodes2 = std::max(maxNodes2, diagnostics.NodesVisited);
            if (status == NavStatus::OutputCapacityReached) ++cap;
            if (status == NavStatus::Success)
            {
                ++ok;
                corners += route.Corners().size();
                deps += route.TileDependencies().size();
                maxCorners = std::max(maxCorners, route.Corners().size());
                maxDeps = std::max(maxDeps, route.TileDependencies().size());
                const auto v0 = Clock::now();
                for (int r = 0; r < 100; ++r)
                    (void)NavValidateRoute(zone, route);
                const auto v1 = Clock::now();
                validateUs.push_back(std::chrono::duration<double, std::micro>(v1 - v0).count() / 100.0);
            }
            else if (status == NavStatus::SearchLimitReached)
                ++limit;
            else
                ++other;
            const auto r0 = Clock::now();
            (void)NavRaycast(zone, context, request, starts[i],
                             starts[i].Position + (ends[i].Position - starts[i].Position) * 0.1f);
            const auto r1 = Clock::now();
            raycastUs.push_back(std::chrono::duration<double, std::micro>(r1 - r0).count());
        }
        const Stats f = Summarize(findUs);
        const Stats v = Summarize(validateUs);
        const Stats p = Summarize(projectUs);
        const Stats r = Summarize(raycastUs);
        std::printf("[probe] %s side=%.0fm minDist=%.0fm maxNodes=%u tiles~%zu\n", label, side,
                    minDistance, maxNodes, cooked.Navigation->Profiles.empty() ? 0 : cooked.Navigation->Profiles[0].Tiles.size());
        std::printf("[probe]   FindRoute us: median %.1f p90 %.1f max %.1f mean %.1f | ok %zu limit %zu other %zu | avg nodes %.0f corners %.1f tileDeps %.1f\n",
                    f.Median, f.P90, f.Max, f.Mean, ok, limit, other,
                    static_cast<double>(nodes) / starts.size(),
                    ok ? static_cast<double>(corners) / ok : 0.0,
                    ok ? static_cast<double>(deps) / ok : 0.0);
        std::printf("[probe]   max nodes %zu max corners %zu max tileDeps %zu capacity-fails %zu\n", maxNodes2, maxCorners, maxDeps, cap);
        std::printf("[probe]   ValidateRoute us: median %.3f p90 %.3f max %.3f\n", v.Median, v.P90, v.Max);
        std::printf("[probe]   ProjectPoint us: median %.2f p90 %.2f | Raycast us: median %.2f p90 %.2f\n",
                    p.Median, p.P90, r.Median, r.P90);
    }
}

TEST(NavRouteCostProbe, Measure)
{
    Probe("open pillars 48m", Arena(48.0f, 4.0f, false), 48.0f, 0.0f, 2048);
    Probe("open pillars 96m", Arena(96.0f, 4.0f, false), 96.0f, 0.0f, 2048);
    Probe("open pillars 96m far", Arena(96.0f, 4.0f, false), 96.0f, 60.0f, 2048);
    Probe("open pillars 96m far 8k", Arena(96.0f, 4.0f, false), 96.0f, 60.0f, 8192);
    Probe("maze 96m", Arena(96.0f, 4.0f, true), 96.0f, 0.0f, 2048);
    Probe("maze 96m 8k", Arena(96.0f, 4.0f, true), 96.0f, 0.0f, 8192);
    Probe("maze 192m", Arena(192.0f, 4.0f, true), 192.0f, 0.0f, 2048);
    Probe("maze 192m 16k", Arena(192.0f, 4.0f, true), 192.0f, 0.0f, 16384);
}
