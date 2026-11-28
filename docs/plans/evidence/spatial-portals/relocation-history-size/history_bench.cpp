// Throwaway: the per-instance history read extraction does, main's 84-byte
// layout against this branch's 116-byte one, in one binary at -O2.
#include <ecs/Query.h>
#include <ecs/World.h>
#include <math/geometry/3d/RigidMapping.h>
#include <world/transform/TransformHistory.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

struct MainHistory
{
    Transform3f Previous;
    Transform3f Current;
    bool Snap = true;
};
SENCHA_DECLARE_COMPONENT_TYPE(MainHistory, "bench.main_history");

static Transform3f ResolveMain(const MainHistory& h, double alpha)
{
    const float t = alpha <= 0.0 ? 0.0f : (alpha >= 1.0 ? 1.0f : static_cast<float>(alpha));
    return Transform3f::Interpolate(h.Previous, h.Current, t);
}

static Transform3f CrossingOf(const WorldTransformHistory& h)
{
    return Transform3f{ h.CrossingTranslation, h.CrossingRotation, Vec3d::One() };
}

static Transform3f ResolveBranch(const WorldTransformHistory& h, double alpha)
{
    const float t = alpha <= 0.0 ? 0.0f : (alpha >= 1.0 ? 1.0f : static_cast<float>(alpha));
    const Transform3f blended = Transform3f::Interpolate(h.Previous, h.Current, t);
    if (h.State == HistoryState::Carrying)
        return CrossingOf(h) * blended;
    if (h.State == HistoryState::Crossing && t < h.CrossedAt)
        return InverseRigidMapping(CrossingOf(h)) * blended;
    return blended;
}

[[gnu::noinline]] static Transform3f AcrossCrossing(const WorldTransformHistory& h, const Transform3f& blended, float t)
{
    if (h.State == HistoryState::Carrying)
        return CrossingOf(h) * blended;
    if (t < h.CrossedAt)
        return InverseRigidMapping(CrossingOf(h)) * blended;
    return blended;
}

static Transform3f ResolveCold(const WorldTransformHistory& h, double alpha)
{
    const float t = alpha <= 0.0 ? 0.0f : (alpha >= 1.0 ? 1.0f : static_cast<float>(alpha));
    const Transform3f blended = Transform3f::Interpolate(h.Previous, h.Current, t);
    if (h.State == HistoryState::Steady || h.State == HistoryState::Snapping) [[likely]]
        return blended;
    return AcrossCrossing(h, blended, t);
}

static Transform3f ResolveBlendOnly(const WorldTransformHistory& h, double alpha)
{
    const float t = alpha <= 0.0 ? 0.0f : (alpha >= 1.0 ? 1.0f : static_cast<float>(alpha));
    return Transform3f::Interpolate(h.Previous, h.Current, t);
}

template <typename H, typename Resolve>
static double PassMs(World& world, Resolve resolve, double alpha, float& sink)
{
    Query<Read<H>> query(world);
    const auto start = std::chrono::steady_clock::now();
    query.ForEachChunk([&](auto& view) {
        const auto rows = view.template Read<H>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            sink += resolve(rows[i], alpha).Position.X;
    });
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int main(int argc, char** argv)
{
    const int count = argc > 1 ? std::atoi(argv[1]) : 100000;
    const int passes = argc > 2 ? std::atoi(argv[2]) : 200;
    World mainWorld;
    mainWorld.RegisterComponent<MainHistory>();
    World branchWorld;
    branchWorld.RegisterComponent<WorldTransformHistory>();
    for (int i = 0; i < count; ++i)
    {
        const Transform3f a{ Vec3d(float(i), 0.0f, 0.0f), Quatf::Identity(), Vec3d::One() };
        const Transform3f b{ Vec3d(float(i) + 1.0f, 0.0f, 0.0f), Quatf::FromAxisAngle(Vec3d::Up(), 0.1f), Vec3d::One() };
        const EntityId m = mainWorld.CreateEntity();
        mainWorld.AddComponent<MainHistory>(m, MainHistory{ a, b, false });
        const EntityId n = branchWorld.CreateEntity();
        branchWorld.AddComponent<WorldTransformHistory>(n, WorldTransformHistory{ .Previous = a, .Current = b, .State = HistoryState::Steady });
    }
    std::vector<double> mainMs, branchMs, layoutMs, coldMs;
    float sink = 0.0f;
    for (int p = 0; p < passes; ++p)
    {
        const double alpha = 0.25 + 0.5 * (p % 7) / 7.0;
        mainMs.push_back(PassMs<MainHistory>(mainWorld, ResolveMain, alpha, sink));
        branchMs.push_back(PassMs<WorldTransformHistory>(branchWorld, ResolveBranch, alpha, sink));
        coldMs.push_back(PassMs<WorldTransformHistory>(branchWorld, ResolveCold, alpha, sink));
        layoutMs.push_back(PassMs<WorldTransformHistory>(branchWorld, ResolveBlendOnly, alpha, sink));
    }
    std::sort(mainMs.begin(), mainMs.end());
    std::sort(branchMs.begin(), branchMs.end());
    std::sort(layoutMs.begin(), layoutMs.end());
    std::sort(coldMs.begin(), coldMs.end());
    const auto pct = [](const std::vector<double>& v, double q) { return v[static_cast<std::size_t>(q * (v.size() - 1))]; };
    std::printf("sizeof main=%zu branch=%zu count=%d passes=%d\n", sizeof(MainHistory), sizeof(WorldTransformHistory), count, passes);
    std::printf("main   ms: min %.3f median %.3f p90 %.3f\n", mainMs.front(), pct(mainMs, 0.5), pct(mainMs, 0.9));
    std::printf("branch ms: min %.3f median %.3f p90 %.3f\n", branchMs.front(), pct(branchMs, 0.5), pct(branchMs, 0.9));
    std::printf("cold-path ms (crossing out of line): median %.3f\n", pct(coldMs, 0.5));
    std::printf("layout ms (116 bytes, no state branch): median %.3f\n", pct(layoutMs, 0.5));
    std::printf("median ratio branch/main %.3f  (ns/instance main %.2f branch %.2f) sink %g\n", pct(branchMs, 0.5) / pct(mainMs, 0.5),
                pct(mainMs, 0.5) * 1e6 / count, pct(branchMs, 0.5) * 1e6 / count, sink);
}
