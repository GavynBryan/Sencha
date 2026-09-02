#include "AssetFieldIo.h"

#include <core/assets/AssetLease.h>
#include <core/assets/AssetRegistry.h>
#include <assets/runtime/AssetSystem.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // The field is type-erased (void* with the offset already applied). Every
    // asset handle is an 8-byte token, and which store the token belongs to is
    // what the field's declared kind answers -- so the bytes move without this
    // code ever naming a handle type.
    std::uint64_t ReadToken(const void* field)
    {
        std::uint64_t token = 0;
        std::memcpy(&token, field, sizeof(token));
        return token;
    }

    void WriteToken(void* field, std::uint64_t token)
    {
        std::memcpy(field, &token, sizeof(token));
    }

    // A path back to a reference: the id looked up through the catalog (invalid
    // when the asset has no stamped id, which ResolveRefPath then treats as
    // path-only). An empty path stays an empty (unset) ref.
    AssetFieldRef RefFromPath(AssetSystem& assets, std::string path, AssetType type)
    {
        AssetFieldRef ref;
        if (!path.empty())
            if (const AssetRecord* record = assets.Resolve(path, type); record != nullptr)
                ref.Id = record->Id;
        ref.Path = std::move(path);
        return ref;
    }

    std::string ResolvePath(AssetSystem& assets, const AssetFieldRef& ref, AssetType type)
    {
        return ref.Path.empty() ? std::string{}
                                : std::string(assets.ResolveRefPath(ref.Id, ref.Path, type));
    }

    // A list's slots are positional (index binds to a mesh section), so an
    // unset member is kept as an empty ref rather than dropped.
    AssetFieldValue ReadAssetList(AssetSystem& assets, AssetType type, std::uint64_t token)
    {
        AssetFieldValue value;
        for (const std::uint64_t member : assets.ListMembers(type, token))
            value.Refs.push_back(
                RefFromPath(assets, std::string(assets.GetPathForLease(type, member)), type));
        return value;
    }

    void ApplyAssetList(AssetSystem& assets, AssetType type, void* field,
                        const AssetFieldValue& value)
    {
        // The members are held here until the new list has taken its own
        // references, and the old list is released only after that, so a member
        // shared between an edited and an unedited slot never reaches zero.
        std::vector<AssetLease> members;
        std::vector<std::uint64_t> tokens;
        for (const AssetFieldRef& ref : value.Refs)
        {
            const std::string path = ResolvePath(assets, ref, type);
            AssetLease member = path.empty() ? AssetLease{} : assets.LoadLease(path, type);
            tokens.push_back(member.OpaqueToken());
            members.push_back(std::move(member));
        }

        AssetLease next = assets.InternList(type, tokens);
        const std::uint64_t old = ReadToken(field);
        WriteToken(field, next.Relinquish()); // the list's reference becomes the field's
        assets.ReleaseLease(type, old, AssetArity::List);
    }
}

AssetFieldValue ReadAssetField(AssetSystem& assets, AssetType type,
                               AssetArity arity, const void* field)
{
    if (arity == AssetArity::List)
        return ReadAssetList(assets, type, ReadToken(field));

    AssetFieldValue value;
    std::string path(assets.GetPathForLease(type, ReadToken(field)));
    if (!path.empty())
        value.Refs.push_back(RefFromPath(assets, std::move(path), type));
    return value;
}

void ApplyAssetField(AssetSystem& assets, AssetType type, AssetArity arity,
                     void* field, const AssetFieldValue& value)
{
    if (arity == AssetArity::List)
    {
        ApplyAssetList(assets, type, field, value);
        return;
    }

    const std::string path = value.Refs.empty()
        ? std::string{}
        : ResolvePath(assets, value.Refs.front(), type);

    const std::uint64_t old = ReadToken(field);
    // The load's own reference becomes the field's: the component's lifecycle
    // hooks release what the field holds, so the lease must not also drop it on
    // the way out of this scope.
    AssetLease next = path.empty() ? AssetLease{} : assets.LoadLease(path, type);
    WriteToken(field, next.Relinquish()); // acquire-then-write
    assets.ReleaseLease(type, old);       // release the replaced handle last
}
