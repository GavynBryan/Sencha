#pragma once

#include <anim/AnimRigData.h>
#include <anim/SkeletonHandle.h>
#include <core/text/InlineString.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>
#include <presentation/PresentationOwner.h>

#include <cstdint>
#include <type_traits>

class World;

// What attachments are stored as; the operations are in attach/Attachment.h.

using AttachmentSocketName = InlineString<kAnimSocketNameMaxLength + 1>;

enum class AttachmentBinding : std::uint8_t
{
    // At its mount's origin, by intent.
    Origin,
    // At a rig socket's joint.
    Joint,
    // Its socket left the rig on a rebind: held where it last was.
    Lost,
};

// On an entity something is attached to: the head of a list through
// Attachment::Next, so a body holds any number.
struct SENCHA_COMPONENT("sencha.attachment_set") AttachmentSet
{
    EntityId First;
    std::uint16_t Count = 0;
};

struct SENCHA_COMPONENT("sencha.attachment") Attachment
{
    // The body whose list holds it, whatever it is mounted on.
    EntityId Root;
    EntityId Mount;
    EntityId Next;
    PresentationOwner Owner;
    AttachmentSocketName Socket;
    Transform3f Offset;
    // The socket's offset and skeleton, from the binding it was resolved in.
    Transform3f SocketOffset;
    SkeletonHandle Skeleton;
    std::uint64_t RigGeneration = 0;
    std::uint16_t Joint = 0;
    AttachmentBinding Binding = AttachmentBinding::Origin;
};

static_assert(std::is_trivially_copyable_v<AttachmentSet> && std::is_trivially_copyable_v<Attachment>,
              "attachment components must be trivially copyable to live in ECS chunks");

template <>
struct ComponentTraits<AttachmentSet>
{
    static void OnRemove(const AttachmentSet& set, World& world, EntityId entity);
};

template <>
struct ComponentTraits<Attachment>
{
    static void OnRemove(const Attachment& attachment, World& world, EntityId entity);
};

#if !defined(SENCHA_CODEGEN)
#  include <attach/AttachmentComponents.sencha.h>
#endif
