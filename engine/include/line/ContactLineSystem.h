#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <core/assets/AssetId.h>
#include <core/assets/AssetRef.h>
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

    SENCHA_LEASE("line.extend")
    SENCHA_LABEL("Extend line")
    SENCHA_CATEGORY("Lines")
    SENCHA_DESCRIPTION("Throws a line with a projectile and keeps it out while held. Letting go draws it home.")
    SENCHA_COMPLETES
    VerbAdmission Extend(const VerbInvocation& invocation,
                         SENCHA_TARGET("instigator") SENCHA_LABEL("Instigator")
                             SENCHA_DESCRIPTION("Whose line it is: where it runs from.") EntityId instigator,
                         SENCHA_ARG("line") SENCHA_LABEL("Line") SENCHA_ASSET(Scene)
                             SENCHA_DESCRIPTION("The prefab: a scene whose root carries Contact Line.") const AssetRef& line,
                         SENCHA_ARG("projectile") SENCHA_LABEL("Projectile") SENCHA_ASSET(Scene)
                             SENCHA_DESCRIPTION("The projectile prefab that throws its tip.") const AssetRef& projectile,
                         SENCHA_ARG("at") SENCHA_LABEL("At") SENCHA_DESCRIPTION("A point to throw it at. The aim when absent.")
                             std::optional<Vec3d> at = std::nullopt,
                         SENCHA_ARG("keep") SENCHA_LABEL("Keep")
                             SENCHA_DESCRIPTION("Left where it stuck when let go, rather than drawn home.")
                                 std::optional<bool> keep = std::nullopt);
    SENCHA_LEASE_RELEASE("line.extend")
    VerbAdmission EndExtend(const VerbInvocation& invocation);
    SENCHA_LEASE_HOLDS("line.extend")
    bool ExtendHolds(const LeaseOwner& owner) const;

    SENCHA_VERB("line.cut")
    SENCHA_LABEL("Cut lines")
    SENCHA_CATEGORY("Lines")
    SENCHA_DESCRIPTION("Ends every line the instigator has out, where it is, striking nothing more.")
    VerbAdmission Cut(const VerbInvocation& invocation, SENCHA_ARG("instigator") SENCHA_LABEL("Instigator") EntityId instigator);

    SENCHA_VERB("line.retract")
    SENCHA_LABEL("Retract lines")
    SENCHA_CATEGORY("Lines")
    SENCHA_DESCRIPTION("Draws home every line the instigator has left anchored.")
    VerbAdmission Retract(SENCHA_ARG("instigator") SENCHA_LABEL("Instigator") EntityId instigator,
                          SENCHA_ARG("keep_newest") SENCHA_LABEL("Keep newest")
                              SENCHA_DESCRIPTION("Leaves the newest of them where it is.")
                                  std::optional<bool> keepNewest = std::nullopt);

    SENCHA_QUERY("line.anchored")
    SENCHA_LABEL("Line anchored")
    SENCHA_CATEGORY("Lines")
    SENCHA_DESCRIPTION("Whether the instigator is holding a line anchored.")
    bool Anchored(SENCHA_ARG("instigator") SENCHA_LABEL("Instigator") EntityId instigator) const;

    SENCHA_QUERY("line.anchored_within")
    SENCHA_LABEL("Line anchored within")
    SENCHA_CATEGORY("Lines")
    SENCHA_DESCRIPTION("Whether the instigator is holding a line anchored within a distance of a point.")
    bool AnchoredWithin(SENCHA_ARG("instigator") SENCHA_LABEL("Instigator") EntityId instigator,
                        SENCHA_ARG("point") SENCHA_LABEL("Point") Vec3d point,
                        SENCHA_ARG("distance") SENCHA_LABEL("Distance") SENCHA_DESCRIPTION("In metres.")
                            float distance) const;

    // Where `instigator` holds a line anchored, if it does. Kept lines it has let
    // go of are not held.
    SENCHA_QUERY("line.anchor")
    SENCHA_LABEL("Line anchor")
    SENCHA_CATEGORY("Lines")
    SENCHA_DESCRIPTION("Where the line the instigator is holding is anchored; unavailable when none is.")
    [[nodiscard]] std::optional<Vec3d> AnchorOf(SENCHA_ARG("instigator") SENCHA_LABEL("Instigator")
                                                    EntityId instigator) const;

    SENCHA_QUERY("line.stuck")
    SENCHA_LABEL("Line stuck")
    SENCHA_CATEGORY("Lines")
    SENCHA_DESCRIPTION("Whether the instigator has let go of a line and left it stuck where it landed.")
    bool Stuck(SENCHA_ARG("instigator") SENCHA_LABEL("Instigator") EntityId instigator) const;

    // The point `shortOf` metres back along the newest line `instigator` has let
    // go of and left stuck, if it has one.
    SENCHA_QUERY("line.stuck_point")
    SENCHA_LABEL("Line stuck point")
    SENCHA_CATEGORY("Lines")
    SENCHA_DESCRIPTION("A point on the newest line the instigator has left stuck, some metres short of where it "
                       "landed; unavailable when none is.")
    [[nodiscard]] std::optional<Vec3d> StuckPoint(SENCHA_ARG("instigator") SENCHA_LABEL("Instigator") EntityId instigator,
                                                  SENCHA_ARG("short_of") SENCHA_LABEL("Short of")
                                                      SENCHA_DESCRIPTION("In metres, back along the line.")
                                                          float shortOf) const;

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    void LetGo(const LeaseOwner& owner, std::uint64_t tick);
    VerbAdmission Throw(const VerbInvocation& invocation, EntityId instigator, const AssetRef& line,
                        const AssetRef& projectile, std::optional<Vec3d> at, bool keep);
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
    void Strike(EntityId line, const ContactLineState& state, GameplayTagId kind, EntityId target, const Vec3d& point,
                const Vec3d& away, const FixedSimTime& now);
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

// Draws home every line that is one of `entities` or is out from one of them.
void RetractContactLinesOf(World& world, std::span<const EntityId> entities);

// Stretches every line's span from its tip to where its instigator is drawn.
// Each frame on a presenting host, before propagation.
void PlaceContactLines(World& world, double presentationAlpha);

#if !defined(SENCHA_CODEGEN)
#  include <line/ContactLineSystem.sencha.h>
#endif
