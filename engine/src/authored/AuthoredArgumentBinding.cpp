#include <assets/data/DataAssetCache.h>
#include <authored/AuthoredArgumentBinding.h>
#include <world/identity/PersistentEntityIndex.h>
VerbAdmission ResolveAuthoredArguments(const AuthoredArguments &constants, std::span<const VerbCompiledInput> bindings,
                                       std::span<const AuthoredValue> inputs, const PersistentEntityIndex *entities,
                                       const DataAssetCache *dataAssets, AuthoredArguments &scratch)
{
    if (inputs.size() != bindings.size())
        return VerbAdmission::InvalidArguments;

    scratch.Resize(constants.Size());
    for (std::size_t slot = 0; slot < constants.Size(); ++slot)
    {
        const AuthoredValue &constant = constants.At(slot);
        PersistentEntityId identity;
        if (!constant.TryGetPersistentEntity(identity))
        {
            scratch.Set(slot, constant);
            continue;
        }
        // The authored relationship, resolved now: whichever entity carries
        // the identity at this moment, or nothing. A target that is absent
        // refuses this request and does not wait for a later one.
        const EntityId entity = entities != nullptr ? entities->TryResolve(identity) : EntityId{};
        if (!entity.IsValid())
            return VerbAdmission::UnresolvedReference;
        scratch.Set(slot, AuthoredValue::Entity(entity));
    }

    for (std::size_t index = 0; index < bindings.size(); ++index)
    {
        for (const VerbInputDestination &destination : bindings[index].Destinations)
        {
            if (!AuthoredValueSatisfiesField(inputs[index], destination.Expected))
                return VerbAdmission::InvalidArguments;
            // A subtype the field declares is checked against the resident
            // value, which is the one thing the kind check above cannot see.
            const DataFieldSchema &expected =
                destination.Expected.Kind == DataFieldKind::Optional && destination.Expected.Children.size() == 1
                    ? destination.Expected.Children.front()
                    : destination.Expected;
            const AssetRef *reference = nullptr;
            if (expected.Kind == DataFieldKind::DataAssetRef && !expected.Reference.DataSubtype.empty() &&
                inputs[index].TryGetDataAsset(reference))
            {
                const DataAssetHandle resident =
                    dataAssets != nullptr ? dataAssets->Find(reference->Path) : DataAssetHandle{};
                if (!resident.IsValid() || dataAssets->GetSubtype(resident) != expected.Reference.DataSubtype)
                {
                    return VerbAdmission::InvalidArguments;
                }
            }
            scratch.Set(destination.ArgumentSlot, inputs[index]);
        }
    }

    return VerbAdmission::Accepted;
}
