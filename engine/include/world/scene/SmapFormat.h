#pragma once

#include <core/assets/AssetId.h>
#include <core/identity/Id.h>
#include <core/json/JsonValue.h>
#include <ecs/ComponentTypeId.h>
#include <math/Vec.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

class AssetRegistry;
class ComponentSerializerRegistry;
class IComponentSerializer;

//=============================================================================
// .smap — the cooked scene container
//
// One fixed little-endian, bounds-checked file replaces the cooked-scene JSON,
// its manifest, and the collision sidecar: a section directory over an
// interned string table, the asset dependency table, the entity records, and
// the collision cells. Runtime streaming parses this; nothing at runtime
// parses scene JSON text.
//
// A component record carries its ComponentTypeId (the content-addressed
// contract key), the schema fingerprint of the serializer that wrote it, and
// a compact value encoding of the component's canonical serialized tree --
// object keys and string values interned through the file-wide string table,
// which is where the size over JSON text comes from. The fingerprint is the
// skew gate: a reader whose serializer no longer matches refuses loudly
// instead of quietly reinterpreting fields.
// The encoding is a value tree rather than a raw
// positional field stream because cooked refs must be writable where no live
// asset handle can exist (a headless cook); the tree decodes into the same
// archive values the one serializer path has always consumed.
//
// Cooked artifacts are derived: a version bump means recook, never migrate.
//=============================================================================

inline constexpr std::uint32_t kSmapMagic = 0x50414D53; // 'SMAP' little-endian
inline constexpr std::uint32_t kSmapVersion = 1;

// Section identities, four-character codes.
inline constexpr std::uint32_t kSmapSectionStrings = 0x53525453;      // 'STRS'
inline constexpr std::uint32_t kSmapSectionDependencies = 0x53504544; // 'DEPS'
inline constexpr std::uint32_t kSmapSectionEntities = 0x53544E45;     // 'ENTS'
inline constexpr std::uint32_t kSmapSectionCollision = 0x4C4C4F43;    // 'COLL'

// One asset this scene needs resident: the stable id when the cook knew it,
// the virtual path always (the dev-build fallback resolver). No type: what an
// asset is comes from the registry record at load time, never from a scene's
// claim about it.
struct SmapDependency
{
    AssetId Id;
    std::string Path;
};

// One collision cell: the .scol blob it references and the cell origin the
// collider spawns at. What the JSON sidecar used to carry.
struct SmapCollisionCell
{
    std::string BlobPath;
    Vec3d Origin{};
};

struct SmapEntityRecord
{
    PersistentEntityId Persistent; // invalid for cook-generated content
    std::uint32_t Parent = UINT32_MAX; // ordinal into Entities, none by default
    std::vector<std::pair<ComponentTypeId, JsonValue>> Components;
};

// The parsed file: what a worker hands the owner-thread import.
struct SmapContents
{
    std::uint64_t ContentHash = 0;
    std::vector<SmapDependency> Dependencies;
    std::vector<SmapCollisionCell> Collision;
    std::vector<SmapEntityRecord> Entities;
};

struct SmapError
{
    std::string Message;
};

// The structural identity of one component's serialized shape: its key and
// every runtime field's name, scalar kind, offset, size, count, and asset
// binding. Computed identically at cook and at load from the same registry,
// which is what makes a mismatch mean "this build's schema is not the one
// that cooked this".
[[nodiscard]] std::uint64_t ComponentSchemaFingerprint(
    const IComponentSerializer& serializer);

// Parses and validates a .smap image. Worker-safe: touches no World, resolves
// no assets. Every component is fingerprint-checked against `serializers`;
// an unknown component id or a fingerprint mismatch refuses the whole file
// with the component named.
[[nodiscard]] bool ReadSmap(std::span<const std::byte> bytes,
                            const ComponentSerializerRegistry& serializers,
                            SmapContents& out,
                            SmapError* error = nullptr);

// ReadSmap over a file's bytes. A missing or unreadable file refuses with the
// path named; everything else is ReadSmap's contract.
[[nodiscard]] bool ReadSmapFile(const std::filesystem::path& path,
                                const ComponentSerializerRegistry& serializers,
                                SmapContents& out,
                                SmapError* error = nullptr);

// Decodes only the tables a consumer needs before any entity exists -- the
// dependency list a preload warms and the collision cells -- leaving
// `out.Entities` empty. No entity payload is decoded and no schema gate runs,
// so this needs no serializer registry; the content hash is still verified.
[[nodiscard]] bool ReadSmapMetadata(std::span<const std::byte> bytes,
                                    SmapContents& out,
                                    SmapError* error = nullptr);

[[nodiscard]] bool ReadSmapMetadataFile(const std::filesystem::path& path,
                                        SmapContents& out,
                                        SmapError* error = nullptr);

// Id-first resolution of the dependency table to the path list a preloader
// consumes: an id the registry knows yields the record's current path
// (rename-proof); anything else falls back to the cooked path. The same
// contract ResolveManifestPaths has always had.
[[nodiscard]] std::vector<std::string> ResolveSmapDependencyPaths(
    std::span<const SmapDependency> dependencies, const AssetRegistry& registry);

// The inverse of ReadSmap, kept beside it so the codec pair cannot drift.
// Fingerprints are computed from `serializers` at write; a component id with
// no registered serializer refuses. The scene-level cook policy that builds
// SmapContents from a cooked document lives in the cook layer.
[[nodiscard]] bool WriteSmap(const SmapContents& contents,
                             const ComponentSerializerRegistry& serializers,
                             std::vector<std::byte>& out,
                             SmapError* error = nullptr);

class EntityBuildPackage;

struct SmapPackageOptions
{
    // Drop authored persistent identity entirely: no import metadata and no
    // persistent_id component. A runtime spawn is transient (v1), so a
    // spawned copy must not collide with authored identity or participate in
    // zone state memory.
    bool StripPersistentIdentity = false;
};

// Converts parsed contents into detached package entities: serialized
// component payloads for owner-thread decode, persistent identity lifted as
// import metadata, parents wired by ordinal. Worker-safe, like ReadSmap.
[[nodiscard]] bool BuildEntityPackageFromSmap(
    const SmapContents& contents,
    const ComponentSerializerRegistry& serializers,
    EntityBuildPackage& package,
    SmapError* error = nullptr);

[[nodiscard]] bool BuildEntityPackageFromSmap(
    const SmapContents& contents,
    const ComponentSerializerRegistry& serializers,
    EntityBuildPackage& package,
    const SmapPackageOptions& options,
    SmapError* error = nullptr);
