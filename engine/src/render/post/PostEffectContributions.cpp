#include <render/post/PostEffectContributions.h>

#include <camera/CameraEffectTiming.h>
#include <ecs/World.h>

#include <algorithm>
#include <cmath>
#include <tuple>

void SetPostEffect(World& world, PostEffectId effect, PostEffectContribution contribution)
{
    if (!world.HasResource<PostEffectContributions>())
        world.AddResource<PostEffectContributions>();
    (void)world.GetResource<PostEffectContributions>().ByEffect.Set(effect, std::move(contribution));
}

void ClearPostEffect(World& world, PostEffectId effect, const PresentationOwner& owner)
{
    if (PostEffectContributions* contributions = world.TryGetResource<PostEffectContributions>())
        (void)contributions->ByEffect.Clear(effect, owner);
}

void ClearPostEffectsOf(World& world, PresentationWriterId writer)
{
    if (PostEffectContributions* contributions = world.TryGetResource<PostEffectContributions>())
        contributions->ByEffect.ClearWriter(writer, [](PostEffectId, const PostEffectContribution&) {});
}

void AdvancePostEffectLifetimes(World& world, float wallDeltaSeconds)
{
    PostEffectContributions* contributions = world.TryGetResource<PostEffectContributions>();
    if (contributions == nullptr || contributions->ByEffect.KeyCount() == 0)
        return;
    contributions->ByEffect.RemoveWhere(
        [&](PostEffectId, const PostEffectContribution& contribution) {
            const LeaseOwner& holder = contribution.Owner.Holder;
            return contribution.Lifetime == PostEffectLifetime::Held && holder.IsSet() && !world.IsAlive(holder.Entity);
        },
        [](PostEffectId, const PostEffectContribution&) {});
    contributions->ByEffect.ForEachKeyMutable([&](PostEffectId, std::vector<PostEffectContribution>& held) {
        for (PostEffectContribution& contribution : held)
            if (contribution.RemainingWallSeconds >= 0.0f)
                contribution.RemainingWallSeconds = std::max(0.0f, contribution.RemainingWallSeconds - wallDeltaSeconds);
    });
    contributions->ByEffect.RemoveWhere(
        [](PostEffectId, const PostEffectContribution& contribution) { return contribution.RemainingWallSeconds == 0.0f; },
        [](PostEffectId, const PostEffectContribution&) {});
}

void ExtractPostEffects(const World& world, EntityId camera, const PostEffectCatalog& catalog, float wallSeconds,
                        PostEffectFrame& out)
{
    out.Entries.clear();
    out.WallSeconds = wallSeconds;
    const PostEffectContributions* contributions = world.TryGetResource<PostEffectContributions>();
    if (contributions == nullptr)
        return;

    contributions->ByEffect.ForEachKey([&](PostEffectId effect, const std::vector<PostEffectContribution>&) {
        const PostEffectType* type = catalog.Get(effect);
        if (type == nullptr)
            return;
        const PostEffectContribution* winner = contributions->ByEffect.Resolve(
            effect, [&](const PostEffectContribution& contribution) {
                return CameraViewIsOwnedBy(world, camera, contribution.AudienceSubject);
            });
        if (winner != nullptr && winner->Weight > 0.0f)
            out.Entries.push_back({ .Effect = effect, .Domain = type->Domain, .Weight = winner->Weight, .Params = winner->Params });
    });

    std::ranges::sort(out.Entries, [&](const PostEffectFrameEntry& a, const PostEffectFrameEntry& b) {
        return std::tuple(a.Domain, catalog.Get(a.Effect)->Order) < std::tuple(b.Domain, catalog.Get(b.Effect)->Order);
    });
}
