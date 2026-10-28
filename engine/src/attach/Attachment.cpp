#include <attach/Attachment.h>

#include <anim/AnimPosePool.h>
#include <anim/AnimPoseState.h>
#include <anim/AnimRig.h>
#include <anim/AnimRigBinding.h>
#include <anim/SkeletonCache.h>
#include <anim/SkinningPalette.h>
#include <core/logging/Logger.h>
#include <ecs/PartitionFollowers.h>
#include <ecs/Query.h>
#include <net/NetReplicationComponents.h>
#include <render/PresentedWith.h>
#include <render/extract/RenderExtractionSystem.h>
#include <render/StaticMeshComponent.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace
{
    // What attachments keep beside the World: entities whose ending a hook could
    // only queue, the cached query the placement walks, and what it has said.
    struct AttachmentRuntime
    {
        Logger* Log = nullptr;
        std::vector<EntityId> PendingDestroy;
        PartitionFollowerId Follower;
        const World* QueryWorld = nullptr;
        std::optional<Query<Write<LocalTransform>, Read<Attachment>>> Placed;
        std::vector<EntityId> Rebind;
        std::vector<Mat4> BindPose;
    };

    AttachmentRuntime* RuntimeOf(World& world) { return world.TryGetResource<AttachmentRuntime>(); }

    const AnimBoundRig* BoundRigOf(World& world, EntityId root)
    {
        if (!world.IsRegistered<AnimRig>())
            return nullptr;
        const AnimRig* rig = world.TryGet<AnimRig>(root);
        AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
        return rig != nullptr && bindings != nullptr ? bindings->Resolve(rig->Rig, world) : nullptr;
    }

    bool IsCosmetic(const World& world, EntityId entity)
    {
        const bool replicated = world.IsRegistered<NetReplicated>() && world.HasComponent<NetReplicated>(entity);
        const bool interpolated =
            world.IsRegistered<WorldTransformHistory>() && world.HasComponent<WorldTransformHistory>(entity);
        return !replicated && !interpolated;
    }

    // The body an entity is worn by when it is itself an attachment, else itself.
    EntityId RootThrough(const World& world, EntityId mount)
    {
        const Attachment* attachment = world.TryGet<Attachment>(mount);
        return attachment != nullptr ? attachment->Root : mount;
    }

    // The body an entity is drawn as part of, through any number of worn bodies.
    EntityId OutermostBody(const World& world, EntityId entity)
    {
        for (const Attachment* attachment = world.TryGet<Attachment>(entity); attachment != nullptr;
             attachment = world.TryGet<Attachment>(entity))
            entity = attachment->Root;
        return entity;
    }

    // Everything `entity` wears, however deep, is drawn as part of `body`.
    void PresentWearablesWith(World& world, EntityId entity, EntityId body)
    {
        std::vector<EntityId> worn;
        ForEachAttachment(world, entity, [&](EntityId wearable, const Attachment&) { worn.push_back(wearable); });
        for (const EntityId wearable : worn)
        {
            if (PresentedWith* with = world.TryGet<PresentedWith>(wearable))
                with->Root = body;
            PresentWearablesWith(world, wearable, body);
        }
    }

    bool MountsOn(const World& world, EntityId mount, EntityId attached)
    {
        for (EntityId at = mount; at.IsValid();)
        {
            if (at == attached)
                return true;
            const Attachment* attachment = world.TryGet<Attachment>(at);
            if (attachment == nullptr || attachment->Mount == at)
                return false;
            at = attachment->Mount;
        }
        return false;
    }

    // Takes `entity` out of its root's list. Field writes only, so a hook may.
    void Unlink(World& world, EntityId entity, EntityId root, EntityId next)
    {
        AttachmentSet* set = world.IsAlive(root) ? world.TryGet<AttachmentSet>(root) : nullptr;
        if (set == nullptr)
            return;
        if (set->First == entity)
        {
            set->First = next;
            return;
        }
        for (EntityId at = set->First; at.IsValid() && world.IsAlive(at);)
        {
            Attachment* attachment = world.TryGet<Attachment>(at);
            if (attachment == nullptr)
                return;
            if (attachment->Next == entity)
            {
                attachment->Next = next;
                return;
            }
            at = attachment->Next;
        }
    }

    void QueueMountedOn(World& world, EntityId root, EntityId mount, std::vector<EntityId>& out)
    {
        ForEachAttachment(world, root, [&](EntityId entity, const Attachment& attachment) {
            if (attachment.Mount == mount && entity != mount)
                out.push_back(entity);
        });
    }

    void DrainPendingDestroy(World& world)
    {
        AttachmentRuntime* runtime = RuntimeOf(world);
        if (runtime == nullptr)
            return;
        // Destroying one may queue what is mounted on it; the swap keeps the
        // list stable while that happens.
        std::vector<EntityId> batch;
        while (!runtime->PendingDestroy.empty())
        {
            batch.swap(runtime->PendingDestroy);
            for (const EntityId entity : batch)
                if (world.IsAlive(entity))
                    world.DestroyEntity(entity);
            batch.clear();
        }
    }

    void FollowRoots(void*, World& world, std::span<const EntityPartitionMove> moves)
    {
        for (const EntityPartitionMove& move : moves)
        {
            if (!world.IsAlive(move.Entity) || world.TryGet<AttachmentSet>(move.Entity) == nullptr)
                continue;
            std::vector<EntityId> attached;
            ForEachAttachment(world, move.Entity, [&](EntityId entity, const Attachment&) { attached.push_back(entity); });
            for (const EntityId entity : attached)
                (void)world.MoveEntityToPartition(entity, move.Current);
        }
    }

    // Moves an attachment, and whatever is mounted on it, onto another root's
    // list and into that root's partition.
    void Reroot(World& world, EntityId entity, EntityId root)
    {
        const Attachment& attachment = *world.TryGet<Attachment>(entity);
        const EntityId previous = attachment.Root;
        std::vector<EntityId> mounted;
        QueueMountedOn(world, previous, entity, mounted);
        Unlink(world, entity, previous, attachment.Next);
        if (world.TryGet<AttachmentSet>(root) == nullptr)
            world.AddComponent<AttachmentSet>(root);
        if (world.GetEntityPartition(entity) != world.GetEntityPartition(root))
            (void)world.MoveEntityToPartition(entity, world.GetEntityPartition(root));
        // Read again: the add and the move both relocate rows.
        Attachment& moved = *world.TryGet<Attachment>(entity);
        AttachmentSet& set = *world.TryGet<AttachmentSet>(root);
        moved.Root = root;
        moved.Next = set.First;
        set.First = entity;
        if (PresentedWith* with = world.TryGet<PresentedWith>(entity))
            with->Root = OutermostBody(world, root);
        for (const EntityId child : mounted)
            Reroot(world, child, root);
    }

    template <typename T>
    void AddOrAssign(World& world, EntityId entity, const T& value)
    {
        if (T* existing = world.TryGet<T>(entity))
            *existing = value;
        else
            world.AddComponent<T>(entity, value);
    }

    // A socket belongs to the root's rig, so an attachment at one is mounted on the root.
    EntityId MountOf(const MeshAttachmentSpec& spec)
    {
        return spec.Socket.empty() && spec.Mount.IsValid() ? spec.Mount : spec.Root;
    }

    // Shared by both kinds of attach once the binding is known.
    AttachResult Link(World& world, EntityId root, EntityId mount, EntityId attached, Attachment fields)
    {
        if (!AttachmentsInstalled(world))
            return AttachResult::NotPresenting;
        if (!world.IsAlive(root) || !world.IsAlive(mount) || !world.IsAlive(attached) || attached == root)
            return AttachResult::NoRoot;
        if (!IsCosmetic(world, attached))
            return AttachResult::NotCosmetic;
        if (MountsOn(world, mount, attached))
            return AttachResult::MountCycle;

        bool rerooted = false;
        if (const Attachment* existing = world.TryGet<Attachment>(attached); existing != nullptr && existing->Root != root)
        {
            Reroot(world, attached, root);
            rerooted = true;
        }
        const bool sameRoot = world.TryGet<Attachment>(attached) != nullptr;

        if (world.GetEntityPartition(attached) != world.GetEntityPartition(root))
            (void)world.MoveEntityToPartition(attached, world.GetEntityPartition(root));
        if (world.TryGet<LocalTransform>(attached) == nullptr)
            (void)ComponentStorageTraits<LocalTransform>::Add(world, attached, LocalTransform{});
        AddOrAssign(world, attached, Parent{ mount });
        // A body that is itself worn is excluded with the body wearing it, and so
        // is whatever it already wears.
        const EntityId body = OutermostBody(world, root);
        AddOrAssign(world, attached, PresentedWith{ body });
        PresentWearablesWith(world, attached, body);

        // Every structural step above may have moved the rows; nothing read
        // before them is used after.
        fields.Root = root;
        fields.Mount = mount;
        if (sameRoot)
        {
            Attachment& current = *world.TryGet<Attachment>(attached);
            fields.Next = current.Next;
            current = fields;
        }
        else
        {
            if (world.TryGet<AttachmentSet>(root) == nullptr)
                world.AddComponent<AttachmentSet>(root);
            AttachmentSet& set = *world.TryGet<AttachmentSet>(root);
            fields.Next = set.First;
            set.First = attached;
            world.AddComponent<Attachment>(attached, fields);
        }
        if (fields.Binding == AttachmentBinding::Origin)
            world.TryGet<LocalTransform>(attached)->Value = fields.Offset;
        return sameRoot && !rerooted ? AttachResult::Updated : AttachResult::Attached;
    }
}

void ComponentTraits<AttachmentSet>::OnRemove(const AttachmentSet& set, World& world, EntityId)
{
    AttachmentRuntime* runtime = RuntimeOf(world);
    if (runtime == nullptr)
        return;
    for (EntityId at = set.First; at.IsValid() && world.IsAlive(at);)
    {
        runtime->PendingDestroy.push_back(at);
        const Attachment* attachment = world.TryGet<Attachment>(at);
        at = attachment != nullptr ? attachment->Next : EntityId{};
    }
}

void ComponentTraits<Attachment>::OnRemove(const Attachment& attachment, World& world, EntityId entity)
{
    Unlink(world, entity, attachment.Root, attachment.Next);
    if (AttachmentRuntime* runtime = RuntimeOf(world))
        QueueMountedOn(world, attachment.Root, entity, runtime->PendingDestroy);
}

AttachResult AttachToSocket(World& world, EntityId root, std::string_view socket, EntityId attached,
                            const AttachPlacement& placement)
{
    if (!AttachmentsInstalled(world))
        return AttachResult::NotPresenting;
    if (!world.IsAlive(root))
        return AttachResult::NoRoot;
    const AnimBoundRig* rig = BoundRigOf(world, root);
    if (rig == nullptr)
        return AttachResult::RootUnposable;
    const RigSocketIndex index = rig->FindSocket(socket);
    if (!index.IsValid())
        return AttachResult::SocketUnknown;

    Attachment fields;
    fields.Owner = placement.Owner;
    fields.Socket = socket;
    fields.Offset = placement.Offset;
    fields.SocketOffset = rig->Sockets[index.Value].Offset;
    fields.Skeleton = rig->Skeleton;
    fields.RigGeneration = rig->Generation;
    fields.Joint = rig->Sockets[index.Value].Joint;
    fields.Binding = AttachmentBinding::Joint;
    return Link(world, root, root, attached, fields);
}

AttachResult AttachToOrigin(World& world, EntityId mount, EntityId attached, const AttachPlacement& placement)
{
    if (!AttachmentsInstalled(world))
        return AttachResult::NotPresenting;
    if (!world.IsAlive(mount))
        return AttachResult::NoRoot;
    Attachment fields;
    fields.Owner = placement.Owner;
    fields.Offset = placement.Offset;
    fields.Binding = AttachmentBinding::Origin;
    return Link(world, RootThrough(world, mount), mount, attached, fields);
}

void Detach(World& world, EntityId attached)
{
    const Attachment* attachment = world.IsAlive(attached) ? world.TryGet<Attachment>(attached) : nullptr;
    if (attachment == nullptr)
        return;
    const EntityId root = attachment->Root;
    std::vector<EntityId> mounted;
    QueueMountedOn(world, root, attached, mounted);
    for (const EntityId child : mounted)
        Reroot(world, child, attached);
    // Its own hook unlinks it; what it carried is already off the old list.
    world.RemoveComponent<Attachment>(attached);
    world.RemoveComponent<PresentedWith>(attached);
    if (world.TryGet<Parent>(attached) != nullptr)
    {
        const Transform3f pose = world.TryGet<WorldTransform>(attached)->Value;
        world.RemoveComponent<Parent>(attached);
        world.TryGet<LocalTransform>(attached)->Value = pose;
    }
    ForEachAttachment(world, attached, [&](EntityId child, const Attachment&) {
        world.TryGet<PresentedWith>(child)->Root = attached;
    });
}

void DestroyAttachment(World& world, EntityId attached)
{
    if (!world.IsAlive(attached) || world.TryGet<Attachment>(attached) == nullptr)
        return;
    world.DestroyEntity(attached);
    DrainPendingDestroy(world);
}

void DestroyAttachmentsOf(World& world, PresentationWriterId writer)
{
    if (!world.IsRegistered<Attachment>())
        return;
    std::vector<EntityId> owned;
    Query<Read<Attachment>> all(world);
    all.ForEachChunk([&](auto& view) {
        const auto attachments = view.template Read<Attachment>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            if (attachments[i].Owner.Writer == writer)
                owned.push_back(view.Entity(i));
    });
    for (const EntityId entity : owned)
        DestroyAttachment(world, entity);
}

EntityId CreateMeshAttachment(World& world, const MeshAttachmentSpec& spec, AttachResult* result)
{
    const auto refuse = [&](AttachResult why) {
        if (result != nullptr)
            *result = why;
        return EntityId{};
    };
    if (!AttachmentsInstalled(world))
        return refuse(AttachResult::NotPresenting);
    if (!world.IsAlive(spec.Root))
        return refuse(AttachResult::NoRoot);

    const EntityId entity = world.CreateEntity(world.GetEntityPartition(spec.Root));
    StaticMeshComponent mesh;
    mesh.Mesh = spec.Mesh;
    mesh.Materials = spec.Materials;
    mesh.Visible = spec.Visible;
    mesh.CastShadows = spec.CastShadows;
    mesh.AffectsBakedLighting = false;
    world.AddComponent<StaticMeshComponent>(entity, mesh);

    const EntityId mount = MountOf(spec);
    const AttachResult attached = spec.Socket.empty()
                                      ? AttachToOrigin(world, mount, entity, spec.Placement)
                                      : AttachToSocket(world, spec.Root, spec.Socket, entity, spec.Placement);
    if (attached != AttachResult::Attached)
    {
        world.DestroyEntity(entity);
        return refuse(attached);
    }
    if (result != nullptr)
        *result = attached;
    return entity;
}

AttachResult UpdateMeshAttachment(World& world, EntityId attached, const MeshAttachmentSpec& spec)
{
    const Attachment* current = world.IsAlive(attached) ? world.TryGet<Attachment>(attached) : nullptr;
    const StaticMeshComponent* drawn = current != nullptr ? world.TryGet<StaticMeshComponent>(attached) : nullptr;
    if (current == nullptr || drawn == nullptr)
        return AttachResult::NoRoot;

    const EntityId mount = MountOf(spec);
    const bool placementChanged = current->Root != spec.Root || current->Mount != mount
                               || current->Socket.View() != spec.Socket || !(current->Offset == spec.Placement.Offset)
                               || !(current->Owner == spec.Placement.Owner);
    if (placementChanged)
    {
        const AttachResult moved = spec.Socket.empty()
                                       ? AttachToOrigin(world, mount, attached, spec.Placement)
                                       : AttachToSocket(world, spec.Root, spec.Socket, attached, spec.Placement);
        if (moved != AttachResult::Attached && moved != AttachResult::Updated)
            return moved;
        drawn = world.TryGet<StaticMeshComponent>(attached);
    }

    if (drawn->Mesh != spec.Mesh || drawn->Materials != spec.Materials || drawn->Visible != spec.Visible
        || drawn->CastShadows != spec.CastShadows)
    {
        StaticMeshComponent mesh = *drawn;
        mesh.Mesh = spec.Mesh;
        mesh.Materials = spec.Materials;
        mesh.Visible = spec.Visible;
        mesh.CastShadows = spec.CastShadows;
        (void)world.ReplaceComponent<StaticMeshComponent>(attached, mesh);
    }
    return AttachResult::Updated;
}

EntityId FindAttachment(const World& world, EntityId root, const PresentationOwner& owner)
{
    EntityId found;
    ForEachAttachment(world, root, [&](EntityId entity, const Attachment& attachment) {
        if (!found.IsValid() && attachment.Owner == owner)
            found = entity;
    });
    return found;
}

void InstallAttachments(World& world, Logger* log)
{
    if (AttachmentsInstalled(world))
        return;
    AttachmentRuntime& runtime = world.AddResource<AttachmentRuntime>();
    runtime.Log = log;
    if (!world.HasResource<PartitionFollowers>())
        world.AddResource<PartitionFollowers>();
    runtime.Follower = world.GetResource<PartitionFollowers>().Register(nullptr, &FollowRoots);
}

bool AttachmentsInstalled(const World& world)
{
    return world.TryGetResource<AttachmentRuntime>() != nullptr && world.IsRegistered<Attachment>();
}

void PlaceAttachments(World& world, double presentationAlpha)
{
    AttachmentRuntime* runtime = RuntimeOf(world);
    if (runtime == nullptr || !world.IsRegistered<Attachment>())
        return;
    DrainPendingDestroy(world);

    if (runtime->QueryWorld != &world)
    {
        runtime->Placed.emplace(world);
        runtime->QueryWorld = &world;
    }
    const AnimPosePool* pool = world.TryGetResource<AnimPosePool>();
    const AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
    const SkeletonCache* skeletons = bindings != nullptr ? bindings->SkeletonSource() : nullptr;
    const bool posed = world.IsRegistered<AnimPoseState>();
    const bool interpolated = world.IsRegistered<WorldTransformHistory>();

    runtime->Rebind.clear();
    const World& reader = world;
    runtime->Placed->ForEachChunk([&](auto& view) {
        const auto locals = view.template Write<LocalTransform>();
        const auto attachments = view.template Read<Attachment>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const Attachment& attachment = attachments[i];
            if (attachment.Owner.Holder.Entity.IsValid() && !reader.IsAlive(attachment.Owner.Holder.Entity))
            {
                runtime->PendingDestroy.push_back(view.Entity(i));
                continue;
            }
            if (attachment.Binding == AttachmentBinding::Origin || !reader.IsAlive(attachment.Root))
                continue;
            const AnimPoseState* state = posed ? reader.TryGet<AnimPoseState>(attachment.Root) : nullptr;
            if (state != nullptr && state->BindingGeneration != 0 && state->BindingGeneration != attachment.RigGeneration)
            {
                runtime->Rebind.push_back(view.Entity(i));
                continue;
            }
            const SkeletonData* skeleton = skeletons != nullptr ? skeletons->Get(attachment.Skeleton) : nullptr;
            if (attachment.Binding == AttachmentBinding::Lost || skeleton == nullptr
                || attachment.Joint >= skeleton->Joints.size())
                continue;

            const AnimPosePool::Slot* slot =
                state != nullptr && pool != nullptr ? pool->Find(state->Slot, attachment.Root) : nullptr;
            Mat4 joint;
            if (MatchSkinnedPose(slot, attachment.Skeleton, skeleton->Joints.size()) == SkinnedPoseMatch::Posed)
            {
                const bool history = interpolated && reader.HasComponent<WorldTransformHistory>(attachment.Root);
                joint = slot->PresentedModel(*skeleton, history ? static_cast<float>(presentationAlpha) : 1.0f)
                            [attachment.Joint];
            }
            else
            {
                // Unposed, or posing another skeleton: drawn at bind, as the mesh is.
                BuildBindModelTransforms(*skeleton, runtime->BindPose);
                joint = runtime->BindPose[attachment.Joint];
            }
            locals[i].Value = Transform3f::FromMat4(joint) * attachment.SocketOffset * attachment.Offset;
        }
    });

    DrainPendingDestroy(world);
    for (const EntityId entity : runtime->Rebind)
    {
        Attachment* attachment = world.TryGet<Attachment>(entity);
        const AnimBoundRig* rig = attachment != nullptr ? BoundRigOf(world, attachment->Root) : nullptr;
        if (attachment == nullptr || rig == nullptr)
            continue;
        const RigSocketIndex index = rig->FindSocket(attachment->Socket.View());
        attachment->RigGeneration = rig->Generation;
        if (!index.IsValid())
        {
            const bool wasBound = attachment->Binding == AttachmentBinding::Joint;
            attachment->Binding = AttachmentBinding::Lost;
            if (runtime->Log != nullptr && wasBound)
                runtime->Log->Warn("attachment_socket_lost: entity {}:{} stays where it was; its rig '{}' no longer "
                                   "has a socket named '{}'.",
                                   entity.Index, entity.Generation, rig->RigPath, attachment->Socket.View());
            continue;
        }
        attachment->Binding = AttachmentBinding::Joint;
        attachment->Joint = rig->Sockets[index.Value].Joint;
        attachment->SocketOffset = rig->Sockets[index.Value].Offset;
        attachment->Skeleton = rig->Skeleton;
    }
}
