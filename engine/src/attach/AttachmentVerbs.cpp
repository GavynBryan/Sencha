#include <attach/AttachmentVerbs.h>

#include <attach/Attachment.h>
#include <authored/AuthoredValue.h>
#include <core/assets/AssetRef.h>
#include <core/assets/AssetStoreTable.h>
#include <ecs/World.h>

#include <algorithm>
#include <array>
#include <string>

namespace
{
    DataFieldSchema Argument(std::string key, DataFieldKind kind, std::string label, std::string description,
                             bool required = true)
    {
        DataFieldSchema field = MakeDataField(kind, std::move(key), std::move(label), std::move(description));
        field.Required = required;
        return field;
    }

    Vec3d VectorArgument(const AuthoredArguments& arguments, std::size_t slot)
    {
        AuthoredVectorValue value;
        if (!arguments.TryGetVector(slot, value))
            return Vec3d::Zero();
        return Vec3d(static_cast<float>(value.Components[0]), static_cast<float>(value.Components[1]),
                     static_cast<float>(value.Components[2]));
    }
}

void DeclareAttachmentVerbs(VerbRegistrationScope& scope)
{
    VerbDefinition attach;
    attach.Name = std::string(kPresentationAttachLease);
    attach.DisplayName = "Hold prop";
    attach.Description = "Shows a mesh at a socket of the target while held.";
    attach.Category = "Presentation";
    attach.Kind = VerbKind::Lease;
    attach.Scope = AuthoredScope::Cosmetic;
    attach.Prediction = AuthoredPrediction::Presentation;
    DataFieldSchema mesh = Argument("mesh", DataFieldKind::AssetRef, "Mesh", "The static mesh to show.");
    mesh.Reference.AssetTypeFilter = AssetType::StaticMesh;
    DataFieldSchema material = Argument("material", DataFieldKind::AssetRef, "Material", "What it draws with.");
    material.Reference.AssetTypeFilter = AssetType::Material;
    DataFieldSchema position = Argument("position", DataFieldKind::Vector, "Offset",
                                        "Metres from the socket, in its space.", false);
    DataFieldSchema rotation = Argument("rotation", DataFieldKind::Vector, "Turn",
                                        "Pitch, yaw and roll from the socket, in degrees.", false);
    DataFieldSchema shadows = Argument("cast_shadows", DataFieldKind::Bool, "Casts shadows",
                                       "Whether it renders into shadow maps.", false);
    shadows.Default = true;
    attach.Arguments.Children = {
        Argument("target", DataFieldKind::Entity, "Target", "The body that holds it."),
        Argument("socket", DataFieldKind::String, "Socket", "The rig socket it is held at, e.g. hand_l."),
        std::move(mesh),
        std::move(material),
        std::move(position),
        std::move(rotation),
        std::move(shadows),
    };
    (void)scope.Declare(std::move(attach));
}

AttachmentOperations::AttachmentOperations(World& world)
    : Entities(&world)
    , WriterId(RegisterPresentationWriter(world, kPresentationAttachLease))
{
}

VerbAdmission AttachmentOperations::Attach(const VerbInvocation& invocation)
{
    std::erase_if(Holds, [&](const Held& held) { return !Entities->IsAlive(held.Attachment); });
    if (invocation.Step == LeaseStep::Release)
    {
        Release(invocation.Owner);
        return VerbAdmission::Accepted;
    }
    // A refused acquire holds nothing, even for a holder that held a prop before it.
    const VerbAdmission admission = Acquire(invocation);
    if (admission != VerbAdmission::Accepted)
        Release(invocation.Owner);
    return admission;
}

VerbAdmission AttachmentOperations::Acquire(const VerbInvocation& invocation)
{
    const AuthoredArguments* arguments = invocation.Arguments;
    EntityId target;
    std::string_view socket;
    const AssetRef* meshRef = nullptr;
    const AssetRef* materialRef = nullptr;
    if (arguments == nullptr || !arguments->TryGetEntity(0, target) || !arguments->TryGetString(1, socket)
        || !arguments->TryGetAsset(2, meshRef) || !arguments->TryGetAsset(3, materialRef))
        return VerbAdmission::InvalidArguments;
    bool castShadows = true;
    (void)arguments->TryGetBool(6, castShadows);
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
    const AssetLease mesh = meshes->TryAcquireLease(meshRef->Path);
    const AssetLease material = materials->TryAcquireLease(materialRef->Path);
    if (!mesh.IsValid() || !material.IsValid())
        return VerbAdmission::Refused;
    const std::array<std::uint64_t, 1> members{ material.OpaqueToken() };
    const AssetLease set = sets->InternList(members);

    const Transform3f offset(VectorArgument(*arguments, 4),
                             Quat<float>::FromPitchYawRollDegrees(VectorArgument(*arguments, 5)), Vec3d::One());
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
