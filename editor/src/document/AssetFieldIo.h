#pragma once

#include <core/assets/AssetId.h>
#include <core/assets/AssetRef.h>

#include <string>
#include <vector>

class AssetSystem;

// One asset reference as the editor carries it: a stable logical id plus the
// path it was known by. Mirrors the engine reference contract (id-first, path
// fallback via AssetSystem::ResolveRefPath): the id follows a rename, the path
// covers assets the id map has not stamped. An empty Path means unset.
struct AssetFieldRef
{
    AssetId     Id{};
    std::string Path;
};

// The value of an asset-handle field as references, independent of the concrete
// handle type. Single-arity fields carry 0..1 refs; List-arity fields (a static
// mesh's per-slot materials) carry 0..N in slot order.
struct AssetFieldValue
{
    std::vector<AssetFieldRef> Refs;
};

// Reads the live handle(s) at `field` (component bytes with the field offset
// already applied) back to references, for display and for snapshotting an
// edit's "before". Dispatches on (type, arity): the one place that knows each
// handle's shape on the live-edit path.
//
// The archive path is SceneFieldCodec<Handle> (world/serialization), kept
// separate on purpose: archive I/O and live-memory edits are different
// operations (merging them would be a fat interface). A new asset-handle type
// updates both.
[[nodiscard]] AssetFieldValue ReadAssetField(AssetSystem& assets, AssetType type,
                                             AssetArity arity, const void* field);

// Resolves `value` (id-first) and writes the handle(s) into `field`, retaining
// the new asset(s) and releasing the previously held one(s) so refcounts stay
// balanced across apply/undo. Acquire-before-release at whole-value granularity:
// for a List the new set is acquired in full before the old set is released, so
// a material shared between an edited and an unedited slot never transits zero.
// An empty value clears the field.
void ApplyAssetField(AssetSystem& assets, AssetType type, AssetArity arity,
                     void* field, const AssetFieldValue& value);
