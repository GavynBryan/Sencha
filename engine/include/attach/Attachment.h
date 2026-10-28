#pragma once

#include <attach/AttachmentComponents.h>
#include <ecs/World.h>
#include <render/MaterialSetHandle.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <cstdint>
#include <string_view>

class Logger;
class World;

// See docs/gameplay/attachments.md.

enum class AttachResult : std::uint8_t
{
    Attached,
    Updated,
    // This World presents nothing, so it holds no attachments.
    NotPresenting,
    NoRoot,
    // The root's rig has no bound socket of that name.
    SocketUnknown,
    // The root has no rig to name a socket on.
    RootUnposable,
    // Replicated or interpolated on its own: not something a body may wear.
    NotCosmetic,
    MountCycle,
};

struct AttachPlacement
{
    Transform3f Offset;
    PresentationOwner Owner;
};

// Structural, so outside any query. `attached` moves into the root's storage
// partition, is parented to its mount, and follows the root from then on.
AttachResult AttachToSocket(World& world, EntityId root, std::string_view socket, EntityId attached,
                            const AttachPlacement& placement = {});
AttachResult AttachToOrigin(World& world, EntityId mount, EntityId attached, const AttachPlacement& placement = {});
// An ordinary entity again, at its last propagated pose, still carrying what is
// mounted on it.
void Detach(World& world, EntityId attached);
// Destroys the attachment and everything mounted on it.
void DestroyAttachment(World& world, EntityId attached);
void DestroyAttachmentsOf(World& world, PresentationWriterId writer);

struct MeshAttachmentSpec
{
    EntityId Root;
    // Empty: the mount's origin, by intent.
    std::string_view Socket;
    // Defaults to Root.
    EntityId Mount;
    AttachPlacement Placement;
    StaticMeshHandle Mesh;
    MaterialSetHandle Materials;
    bool Visible = true;
    bool CastShadows = true;
};

// Invalid, with nothing created, unless `result` reports it attached.
EntityId CreateMeshAttachment(World& world, const MeshAttachmentSpec& spec, AttachResult* result = nullptr);
// In place: the entity is kept, and its mesh changes keep their leases balanced.
AttachResult UpdateMeshAttachment(World& world, EntityId attached, const MeshAttachmentSpec& spec);

[[nodiscard]] EntityId FindAttachment(const World& world, EntityId root, const PresentationOwner& owner);

template <typename Visit>
void ForEachAttachment(const World& world, EntityId root, Visit&& visit);

// The resources and partition follower attachments need, on a World that
// presents. Every operation above refuses on a World without them.
void InstallAttachments(World& world, Logger* log);
[[nodiscard]] bool AttachmentsInstalled(const World& world);

// Once per frame, before presentation propagation: ends what died, rebinds what
// a rig reload moved, and places each joint attachment at its root's pose.
void PlaceAttachments(World& world, double presentationAlpha);

template <typename Visit>
void ForEachAttachment(const World& world, EntityId root, Visit&& visit)
{
    if (!world.IsRegistered<AttachmentSet>() || !world.IsAlive(root))
        return;
    const AttachmentSet* set = world.TryGet<AttachmentSet>(root);
    for (EntityId at = set != nullptr ? set->First : EntityId{}; at.IsValid() && world.IsAlive(at);)
    {
        const Attachment* attachment = world.TryGet<Attachment>(at);
        if (attachment == nullptr)
            return;
        visit(at, *attachment);
        at = attachment->Next;
    }
}
