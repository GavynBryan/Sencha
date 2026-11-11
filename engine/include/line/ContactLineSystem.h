#pragma once

#include <authored/AuthoredQueryDispatcher.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <core/assets/AssetId.h>
#include <ecs/EntityId.h>
#include <ecs/LeaseOwner.h>
#include <ecs/Query.h>
#include <line/ContactLine.h>
#include <math/Vec.h>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

class EngineSchedule;
class INetPrefabSpawner;
class PhysicsWorld;
class ProjectileSystem;
class ResponsesSystem;
class World;
struct FixedLogicContext;
struct FixedSimTime;

// Throw a line at a point or along the aim, and keep it out while held.
inline constexpr std::string_view kLineExtendLease = "line.extend";
// End every line an entity has out, where it is.
inline constexpr std::string_view kLineCutVerb = "line.cut";
// Draw home every line an entity has left anchored.
inline constexpr std::string_view kLineRetractVerb = "line.retract";
// Whether an entity has a line it is holding anchored.
inline constexpr std::string_view kLineAnchoredQuery = "line.anchored";
// Whether an entity has a line it is holding anchored within a distance of a point.
inline constexpr std::string_view kLineAnchoredWithinQuery = "line.anchored_within";
// Where the line an entity is holding is anchored.
inline constexpr std::string_view kLineAnchorQuery = "line.anchor";
// Whether an entity has a line it let go of still stuck where it landed.
inline constexpr std::string_view kLineStuckQuery = "line.stuck";
// A point along the newest such line, some metres short of where it is stuck.
inline constexpr std::string_view kLineStuckPointQuery = "line.stuck_point";

void DeclareContactLineVocabulary(VerbRegistrationScope& verbs, AuthoredQueryRegistrationScope& queries);

//=============================================================================
// ContactLineSystem
//
// Throws lines and, on the simulation authority, keeps each one: the tip follows
// its projectile out and stays where the projectile met level geometry; the line
// strikes characters that touch it while it is out; something solid across it
// cuts it once it is out; a throw is committed, so letting go in flight changes
// nothing until it stops; letting go after draws the tip home, striking nothing, unless it was kept: a
// kept line stays anchored and leashes its instigator until it times out or is
// drawn home. One whose instigator has gone ends. See docs/gameplay/contact-lines.md.
//=============================================================================
class ContactLineSystem
{
public:
    struct Services
    {
        const PhysicsWorld* Physics = nullptr;
        INetPrefabSpawner* Prefabs = nullptr;
        ProjectileSystem* Projectiles = nullptr;
        // Where strikes are delivered; none in a host without responses.
        ResponsesSystem* Responses = nullptr;
    };

    ContactLineSystem(World& world, const Services& services);

    void FixedLogic(FixedLogicContext& ctx);

    VerbAdmission Extend(const VerbInvocation& invocation);
    VerbAdmission Cut(const VerbInvocation& invocation);
    VerbAdmission Retract(const VerbInvocation& invocation);

    // Where `instigator` holds a line anchored, if it does. Kept lines it has let
    // go of are not held.
    [[nodiscard]] std::optional<Vec3d> AnchorOf(EntityId instigator) const;
    // The point `shortOf` metres back along the newest line `instigator` has let
    // go of and left stuck, if it has one.
    [[nodiscard]] std::optional<Vec3d> StuckPoint(EntityId instigator, float shortOf) const;

private:
    void LetGo(const LeaseOwner& owner, std::uint64_t tick);
    VerbAdmission Throw(const VerbInvocation& invocation);
    VerbAdmission LaunchTip(ContactLineState& state, const ContactLine& authored, AssetId shot,
                            const std::optional<Vec3d>& at, std::uint64_t tick);
    void Step(EntityId line, const FixedSimTime& now);
    void FollowTip(EntityId line, ContactLineState& state, const ContactLine& authored, std::uint64_t tick);
    void WriteBack(EntityId line, const ContactLineState& state, const Vec3d& root);
    // False when something solid has cut the line.
    bool StrikeAlong(EntityId line, ContactLineState& state, const ContactLine& authored, const Vec3d& root,
                     const FixedSimTime& now);
    [[nodiscard]] bool CrossesSolid(const ContactLineState& state, const Vec3d& root, const Vec3d& direction,
                                    float length) const;
    void Strike(EntityId line, const ContactLineState& state, EntityId target, const Vec3d& point, const Vec3d& away,
                const FixedSimTime& now);
    void CueAt(const ContactLineState& state, EntityId line, GameplayTagId cue, const Vec3d& point, std::uint64_t tick);
    // Ends the line, and its throw if that is still in the air: a tip nobody can
    // see must not go on striking.
    void Remove(EntityId line, std::uint64_t tick);
    // Holds each instigator within the tightest of its kept lines.
    void Leash(std::span<const EntityId> lines);
    void GatherLeashes(std::span<const EntityId> lines);

    World* Entities;
    Services Host;
    std::optional<Query<Read<ContactLineState>>> Lines;
    std::vector<EntityId> Scratch;
    std::vector<EntityId> Overlaps;
    struct LeashEntry
    {
        EntityId Instigator;
        EntityId Line;
        Vec3d Tip = Vec3d::Zero();
        float Allowed = 0.0f;
        float Margin = 0.0f;
    };
    std::vector<LeashEntry> Leashes;
};

ContactLineSystem& RegisterContactLineSystem(EngineSchedule& schedule, World& world,
                                             const ContactLineSystem::Services& services);

inline VerbAdmission InvokeLineExtend(ContactLineSystem& system, const VerbInvocation& invocation)
{
    return system.Extend(invocation);
}

inline VerbAdmission InvokeLineCut(ContactLineSystem& system, const VerbInvocation& invocation)
{
    return system.Cut(invocation);
}

inline VerbAdmission InvokeLineRetract(ContactLineSystem& system, const VerbInvocation& invocation)
{
    return system.Retract(invocation);
}

[[nodiscard]] std::vector<AuthoredQueryBindingToken> BindContactLineQueries(AuthoredQueryDispatcher& dispatcher,
                                                                            const ContactLineSystem& system);

// Where a line runs from: its instigator's aim origin, moved to its slot.
[[nodiscard]] Vec3d ContactLineRoot(const World& world, const ContactLineState& state);

// A line that is out, root to tip.
struct ContactLineSegment
{
    EntityId Instigator;
    Vec3d From = Vec3d::Zero();
    Vec3d To = Vec3d::Zero();
    bool Kept = false;
    // Stopped in level geometry, and how long ago.
    bool Anchored = false;
    float AnchoredSeconds = 0.0f;
    // Something is still holding it, as a throw holds its line until it lands.
    bool Held = false;
};

// Every line not on its way home.
void CollectContactLines(const World& world, std::vector<ContactLineSegment>& out);

// Draws home `instigator`'s kept lines, all of them or all but the newest.
void RetractKeptContactLines(World& world, EntityId instigator, bool keepNewest = false);

// Stretches every line's span from its tip to where its instigator is drawn.
// Each frame on a presenting host, before propagation.
void PlaceContactLines(World& world, double presentationAlpha);
