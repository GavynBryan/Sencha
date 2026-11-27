#include <attach/AttachmentVerbs.h>

#include <attach/AttachedLight.h>
#include <attach/Attachment.h>
#include <camera/CameraEffectArguments.h>
#include <core/assets/AssetRef.h>
#include <core/assets/AssetStoreTable.h>
#include <ecs/World.h>
#include <time/SimClock.h>

#include <algorithm>
#include <array>

AttachmentOperations::AttachmentOperations(World& world)
    : Entities(&world)
    , WriterId(RegisterPresentationWriter(world, AuthoredApiDefinition<AttachmentOperations>::Name_Attach))
{
}

VerbAdmission AttachmentOperations::Attach(const VerbInvocation& invocation, EntityId target, std::string_view socket,
                                          const AssetRef& mesh, const AssetRef& material,
                                          std::optional<Vec3d> position, std::optional<Vec3d> rotation,
                                          bool castShadows)
{
    std::erase_if(Holds, [&](const Held& held) { return !Entities->IsAlive(held.Attachment); });
    const Transform3f offset(position.value_or(Vec3d::Zero()),
                             Quat<float>::FromPitchYawRollDegrees(rotation.value_or(Vec3d::Zero())), Vec3d::One());
    const VerbAdmission admission = Acquire(invocation, target, socket, mesh, material, offset, castShadows);
    if (admission != VerbAdmission::Accepted)
        Release(invocation.Owner);
    return admission;
}

VerbAdmission AttachmentOperations::EndAttach(const VerbInvocation& invocation)
{
    std::erase_if(Holds, [&](const Held& held) { return !Entities->IsAlive(held.Attachment); });
    Release(invocation.Owner);
    return VerbAdmission::Accepted;
}

VerbAdmission AttachmentOperations::Acquire(const VerbInvocation& invocation, EntityId target, std::string_view socket,
                                           const AssetRef& meshRef, const AssetRef& materialRef,
                                           const Transform3f& offset, bool castShadows)
{
    if (!AttachmentsInstalled(*Entities))
        return VerbAdmission::Unavailable;

    const AssetStoreTable* stores = Entities->TryGetResource<AssetStoreTable>();
    IAssetStore* meshes = stores != nullptr ? stores->Find(AssetType::StaticMesh, AssetArity::Single) : nullptr;
    IAssetStore* materials = stores != nullptr ? stores->Find(AssetType::Material, AssetArity::Single) : nullptr;
    IAssetListStore* sets = stores != nullptr ? stores->FindList(AssetType::Material) : nullptr;
    if (meshes == nullptr || materials == nullptr || sets == nullptr)
        return VerbAdmission::Unavailable;
    // Leases for the length of this call: the attachment's mesh component
    // takes its own references when it is added.
    const AssetLease mesh = meshes->TryAcquireLease(meshRef.Path);
    const AssetLease material = materials->TryAcquireLease(materialRef.Path);
    if (!mesh.IsValid() || !material.IsValid())
        return VerbAdmission::Refused;
    const std::array<std::uint64_t, 1> members{ material.OpaqueToken() };
    const AssetLease set = sets->InternList(members);

    const MeshAttachmentSpec spec{ .Root = target,
                                   .Socket = socket,
                                   .Mount = {},
                                   .Placement = { .Offset = offset, .Owner = { WriterId, invocation.Owner } },
                                   .Mesh = StaticMeshHandle::FromToken(mesh.OpaqueToken()),
                                   .Materials = MaterialSetHandle::FromToken(set.OpaqueToken()),
                                   .Visible = true,
                                   .CastShadows = castShadows };

    // Acquired again for the same holder, as a late joiner's first sight does:
    // the attachment it holds changes in place.
    if (const auto held = std::find_if(Holds.begin(), Holds.end(),
                                       [&](const Held& h) { return h.Owner == invocation.Owner; });
        held != Holds.end())
        return UpdateMeshAttachment(*Entities, held->Attachment, spec) == AttachResult::Updated
                   ? VerbAdmission::Accepted
                   : VerbAdmission::Refused;

    AttachResult result = AttachResult::NoRoot;
    const EntityId attached = CreateMeshAttachment(*Entities, spec, &result);
    if (!attached.IsValid())
        return result == AttachResult::NotPresenting ? VerbAdmission::Unavailable : VerbAdmission::Refused;
    Holds.push_back(Held{ invocation.Owner, attached });
    return VerbAdmission::Accepted;
}

void AttachmentOperations::Release(const LeaseOwner& owner)
{
    std::erase_if(Holds, [&](const Held& held) {
        if (held.Owner != owner)
            return false;
        DestroyAttachment(*Entities, held.Attachment);
        return true;
    });
}

AttachedLightOperations::AttachedLightOperations(World& world, const FixedSimulationLoop& clock)
    : Entities(&world)
    , Clock(&clock)
    , WriterId(RegisterPresentationWriter(world, AuthoredApiDefinition<AttachedLightOperations>::Name_Light))
{
}

void AttachedLightOperations::Release(const VerbInvocation& invocation)
{
    if (AttachedLightState* state = Entities->TryGetResource<AttachedLightState>())
        state->Release(invocation.Owner, CameraEffectTickOf(invocation, *Clock), Clock->GetFixedDt());
}

VerbAdmission AttachedLightOperations::EndLight(const VerbInvocation& invocation)
{
    if (Entities->TryGetResource<AttachedLightState>() == nullptr)
        return VerbAdmission::Unavailable;
    Release(invocation);
    return VerbAdmission::Accepted;
}

VerbAdmission AttachedLightOperations::Light(const VerbInvocation& invocation, EntityId target,
                                             std::optional<std::string_view> socket, std::optional<Vec3d> position,
                                             Vec3d color, float intensity, float range, float peakScale,
                                             float pulseSeconds, float attack, float release, bool packsFirst)
{
    AttachedLightState* state = Entities->TryGetResource<AttachedLightState>();
    if (state == nullptr)
        return VerbAdmission::Unavailable;
    AttachedLightSpec spec;
    spec.Envelope.Attack = attack;
    spec.Envelope.Release = release;
    spec.PulseSeconds = pulseSeconds;
    spec.PeakScale = peakScale;
    if (!IsValidCameraEnvelope(spec.Envelope))
    {
        Release(invocation);
        return VerbAdmission::InvalidArguments;
    }
    PointLightComponent& light = spec.Attachment.Light;
    light.Color = color;
    light.Intensity = intensity;
    light.Range = range;
    light.PacksFirst = packsFirst;
    spec.Attachment.Root = target;
    spec.Attachment.Socket = socket.value_or("");
    spec.Attachment.Placement = { .Offset = Transform3f(position.value_or(Vec3d::Zero()), Quat<float>::Identity(),
                                                        Vec3d::One()),
                                  .Owner = { WriterId, invocation.Owner } };

    const AttachResult result = state->Hold(*Entities, spec, CameraEffectTickOf(invocation, *Clock), invocation.Owner);
    if (result == AttachResult::Attached || result == AttachResult::Updated)
        return VerbAdmission::Accepted;
    Release(invocation);
    return result == AttachResult::NotPresenting ? VerbAdmission::Unavailable : VerbAdmission::Refused;
}
