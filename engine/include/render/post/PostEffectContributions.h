#pragma once

#include <ecs/EntityId.h>
#include <presentation/PresentationContributions.h>
#include <presentation/PresentationOwner.h>
#include <render/post/PostEffectCatalog.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

class World;

template <>
struct std::hash<PostEffectId>
{
    [[nodiscard]] std::size_t operator()(PostEffectId id) const noexcept { return id.Value; }
};

enum class PostEffectLifetime : std::uint8_t
{
    // Ends with its holder entity, or by an explicit clear.
    Held,
    // Survives pause, level unload and pawn destruction: ends by an explicit clear,
    // by clearing its writer, or when RemainingWallSeconds runs out.
    Session,
};

inline constexpr std::size_t kPostEffectParamCount = 16;

// One writer's request to run an effect. Its owner drives Weight every frame on
// whatever clock it chooses. Presentation-only: never replicated, never saved,
// never read by a fixed-tick system.
struct PostEffectContribution
{
    PresentationOwner Owner;
    // The winner per effect type per view is the highest, then the owner
    // PresentationOwnerPrecedes puts first.
    std::int16_t Order = 0;
    // The camera subject whose view shows this; none means every view of this World.
    EntityId AudienceSubject;
    float Weight = 0.0f;
    std::array<float, kPostEffectParamCount> Params{};
    PostEffectLifetime Lifetime = PostEffectLifetime::Held;
    // Wall seconds until it ends on its own; negative means no cap.
    float RemainingWallSeconds = -1.0f;
};

// A World resource: a view sees only its own World's contributions.
struct PostEffectContributions
{
    PresentationContributions<PostEffectId, PostEffectContribution> ByEffect;
};

void SetPostEffect(World& world, PostEffectId effect, PostEffectContribution contribution);
void ClearPostEffect(World& world, PostEffectId effect, const PresentationOwner& owner);
void ClearPostEffectsOf(World& world, PresentationWriterId writer);

// Ends held contributions whose holder has gone and sessions whose cap has run out.
void AdvancePostEffectLifetimes(World& world, float wallDeltaSeconds);

struct PostEffectFrameEntry
{
    PostEffectId Effect;
    PostEffectDomain Domain = PostEffectDomain::Display;
    float Weight = 0.0f;
    std::array<float, kPostEffectParamCount> Params{};
};

// What the chain runs this frame for one camera, in (domain, registered order).
struct PostEffectFrame
{
    std::vector<PostEffectFrameEntry> Entries;
    // Wall seconds that keep advancing while the simulation is paused.
    float WallSeconds = 0.0f;
};

// A contribution at weight 0 is not in the result, so an idle effect costs no pass.
void ExtractPostEffects(const World& world, EntityId camera, const PostEffectCatalog& catalog, float wallSeconds,
                        PostEffectFrame& out);
