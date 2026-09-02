// AssetFieldIo: the editor's live-handle asset I/O. The List (per-slot material)
// and Data (structured data) paths are the ones that carry refcount risk, so
// they are exercised headlessly here against real caches. The mesh Single paths
// need a graphics-backed cache and are covered at runtime, not here.

#include "document/AssetFieldIo.h"

#include <assets/material/MaterialAssetLoader.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/runtime/RegisterAssetKind.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/RuntimeSchema.h>
#include <movement/components/CharacterMovement.h>
#include <movement/MovementProfileData.h>
#include <movement/MovementTuningSourceSerializer.h>
#include <render/Material.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/IComponentSerializer.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
    // A sink-less logger plus the material caches, wired into an AssetSystem the
    // way the editor wires the real one (only the material kind is needed here).
    struct AssetFieldFixture
    {
        LoggingProvider     Logging;
        AssetRegistry       Registry{ Logging };
        MaterialCache       Materials;
        MaterialSetCache    Sets{ &Materials };
        MaterialAssetLoader Loader{ Logging, &Materials, nullptr };
        AssetSystem         Assets{ Logging, Registry };

        AssetFieldFixture()
        {
            RegisterAssetKind(Assets, AssetType::Material, Loader, &Materials, &Sets);
        }

        // A procedural material: a record for the path, and a resident entry
        // the caller holds one reference to.
        MaterialHandle Register(const char* path)
        {
            Registry.RegisterOrVerify(AssetRecord{
                .Type = AssetType::Material,
                .SourceKind = AssetSourceKind::Procedural,
                .Path = path,
            });
            return Materials.Register(path, Material{});
        }

        void ReleaseList(const MaterialSetHandle& field)
        {
            Assets.ReleaseLease(AssetType::Material, field.ToToken(), AssetArity::List);
        }
    };

    AssetFieldValue Value(std::initializer_list<const char*> paths)
    {
        AssetFieldValue value;
        for (const char* path : paths)
            value.Refs.push_back(AssetFieldRef{ {}, path });
        return value;
    }

    AssetFieldValue ReadList(AssetSystem& assets, const MaterialSetHandle& field)
    {
        return ReadAssetField(assets, AssetType::Material, AssetArity::List, &field);
    }

    void WriteList(AssetSystem& assets, MaterialSetHandle& field, const AssetFieldValue& value)
    {
        ApplyAssetField(assets, AssetType::Material, AssetArity::List, &field, value);
    }
}

TEST(AssetFieldIo, ListRoundTripPreservesUneditedSlots)
{
    AssetFieldFixture f;
    const char* a = "asset://m/a.smat";
    const char* b = "asset://m/b.smat";
    const char* c = "asset://m/c.smat";
    f.Register(a);
    f.Register(b);
    f.Register(c);

    MaterialSetHandle field{};
    WriteList(f.Assets, field, Value({ a, b }));

    AssetFieldValue read = ReadList(f.Assets, field);
    ASSERT_EQ(read.Refs.size(), 2u);
    EXPECT_EQ(read.Refs[0].Path, a);
    EXPECT_EQ(read.Refs[1].Path, b);

    // Edit slot 1 only; slot 0 must come back unchanged.
    WriteList(f.Assets, field, Value({ a, c }));
    read = ReadList(f.Assets, field);
    ASSERT_EQ(read.Refs.size(), 2u);
    EXPECT_EQ(read.Refs[0].Path, a);
    EXPECT_EQ(read.Refs[1].Path, c);

    f.ReleaseList(field);
}

// The transient-zero guard. After dropping the registration references, the set
// is the only holder of a and b. Editing slot 1 leaves slot 0 (a) shared between
// the old and new sets; releasing the old set before acquiring the new one would
// free a mid-edit, and a procedural material cannot reload, so the slot would
// come back invalid. Acquire-before-release at set granularity keeps a alive.
TEST(AssetFieldIo, SharedMaterialSurvivesAcrossPartialEdit)
{
    AssetFieldFixture f;
    const char* a = "asset://m/a.smat";
    const char* b = "asset://m/b.smat";
    const char* c = "asset://m/c.smat";
    const MaterialHandle hA = f.Register(a);
    const MaterialHandle hB = f.Register(b);
    f.Register(c); // the incoming material must stay resident to load

    MaterialSetHandle field{};
    WriteList(f.Assets, field, Value({ a, b }));

    // The set now holds a and b; drop the registration refs so it is the only one.
    f.Materials.Release(hA);
    f.Materials.Release(hB);
    ASSERT_NE(f.Materials.Get(hA), nullptr);
    ASSERT_NE(f.Materials.Get(hB), nullptr);

    WriteList(f.Assets, field, Value({ a, c }));

    EXPECT_NE(f.Materials.Get(hA), nullptr); // shared slot 0 survived the swap
    EXPECT_EQ(f.Materials.Get(hB), nullptr); // only the old set held b; it is gone

    const AssetFieldValue read = ReadList(f.Assets, field);
    ASSERT_EQ(read.Refs.size(), 2u);
    EXPECT_EQ(read.Refs[0].Path, a);
    EXPECT_EQ(read.Refs[1].Path, c);

    f.ReleaseList(field);
}

// id-first resolution: a ref carrying the stable id but a stale path (the case an
// undo holds after the asset was renamed) resolves to the asset's current path.
// A Single-arity reference of a kind nothing enumerated. The dispatch this
// replaced listed six (type, arity) pairs and aborted on anything else, so a
// single material -- or a texture, which ZoneLightmapComponent actually
// declares -- reached an assert instead of a value. Nothing is enumerated now:
// the kind comes from the field, so the shape works for kinds no one has added
// yet.
TEST(AssetFieldIo, ASingleReferenceWorksForAKindNothingEnumerated)
{
    AssetFieldFixture f;
    const char* path = "asset://m/single.smat";
    f.Register(path);

    MaterialHandle field{};
    ApplyAssetField(f.Assets, AssetType::Material, AssetArity::Single, &field, Value({ path }));

    const AssetFieldValue read =
        ReadAssetField(f.Assets, AssetType::Material, AssetArity::Single, &field);
    ASSERT_EQ(read.Refs.size(), 1u);
    EXPECT_EQ(read.Refs[0].Path, path);

    // And clearing it lets the reference go rather than stranding it.
    ApplyAssetField(f.Assets, AssetType::Material, AssetArity::Single, &field, Value({}));
    EXPECT_FALSE(field.IsValid());
    EXPECT_TRUE(ReadAssetField(f.Assets, AssetType::Material, AssetArity::Single, &field)
                    .Refs.empty());
}

TEST(AssetFieldIo, ResolvesRefByIdWhenPathIsStale)
{
    AssetFieldFixture f;
    const char* current = "asset://m/current.smat";
    f.Register(current);
    const AssetId id{ 0x00ABCDEFull };
    ASSERT_TRUE(f.Registry.AssignId(current, id));

    AssetFieldValue value;
    value.Refs.push_back(AssetFieldRef{ id, "asset://m/old-name.smat" });

    MaterialSetHandle field{};
    WriteList(f.Assets, field, value);

    const AssetFieldValue read = ReadList(f.Assets, field);
    ASSERT_EQ(read.Refs.size(), 1u);
    EXPECT_EQ(read.Refs[0].Path, current); // the id won over the stale path

    f.ReleaseList(field);
}

// A ref to an asset that no longer exists (no id, unknown path: the undo-after-
// remove case) resolves to an empty slot without crashing.
TEST(AssetFieldIo, MissingRefResolvesToEmptySlot)
{
    AssetFieldFixture f;

    AssetFieldValue value;
    value.Refs.push_back(AssetFieldRef{ {}, "asset://m/gone.smat" });

    MaterialSetHandle field{};
    WriteList(f.Assets, field, value);

    const AssetFieldValue read = ReadList(f.Assets, field);
    ASSERT_EQ(read.Refs.size(), 1u);
    EXPECT_TRUE(read.Refs[0].Path.empty());

    f.ReleaseList(field);
}

//=============================================================================
// Structured data (AssetType::Data, Single)
//
// The one kind the asset front door cannot name a handle type for: it travels
// as an opaque token, so the read resolves through GetPathForLease and the
// apply moves a lease's reference into the field. A full headless RuntimeAssets
// is the fixture because the data stack -- subtype registry, schemas, loader,
// cache -- lives there rather than in AssetSystem.
//=============================================================================
namespace
{
    // A movement profile on disk, registered so the asset system can resolve
    // it. The profile subtype is registered by RuntimeAssets itself.
    class TempProfileFile
    {
    public:
        explicit TempProfileFile(std::string_view name)
        {
            static int counter = 0;
            File = std::filesystem::temp_directory_path()
                / ("sencha_profile_" + std::string(name) + "_"
                   + std::to_string(++counter) + ".sdata");
            Virtual = "asset://data/" + std::string(name) + ".sdata";

            std::ofstream out(File, std::ios::trunc);
            out << R"({"type":"movement.profile","version":1,"data":{"name":")"
                << name << R"(","layers":[{"name":"Base","set":{"max_speed":5}}]}})";
        }

        ~TempProfileFile()
        {
            std::error_code ec;
            std::filesystem::remove(File, ec);
        }

        TempProfileFile(const TempProfileFile&) = delete;
        TempProfileFile& operator=(const TempProfileFile&) = delete;

        void RegisterIn(AssetRegistry& registry) const
        {
            AssetRecord record;
            record.Type = AssetType::Data;
            record.SourceKind = AssetSourceKind::File;
            record.Path = Virtual;
            record.FilePath = File.generic_string();
            ASSERT_TRUE(registry.Register(record));
        }

        std::filesystem::path File;
        std::string           Virtual;
    };

    AssetFieldValue ReadProfile(AssetSystem& assets, const MovementProfileHandle& field)
    {
        return ReadAssetField(assets, AssetType::Data, AssetArity::Single, &field);
    }

    void WriteProfile(AssetSystem& assets, MovementProfileHandle& field,
                      const AssetFieldValue& value)
    {
        ApplyAssetField(assets, AssetType::Data, AssetArity::Single, &field, value);
    }
}

TEST(AssetFieldIo, DataFieldRoundTripsThroughItsPath)
{
    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets(logging, serializers);
    TempProfileFile profile("walk");
    profile.RegisterIn(assets.Registry);

    MovementProfileHandle field{};
    EXPECT_TRUE(ReadProfile(assets.Assets, field).Refs.empty());

    WriteProfile(assets.Assets, field, Value({ profile.Virtual.c_str() }));
    ASSERT_TRUE(field.IsValid());

    const AssetFieldValue read = ReadProfile(assets.Assets, field);
    ASSERT_EQ(read.Refs.size(), 1u);
    EXPECT_EQ(read.Refs[0].Path, profile.Virtual);

    // Clearing the field releases the last reference, so the entry is freed.
    WriteProfile(assets.Assets, field, AssetFieldValue{});
    EXPECT_FALSE(field.IsValid());
    EXPECT_FALSE(assets.DataAssets.Find(profile.Virtual).IsValid());
}

// The refcount claim, stated as residency: after an edit the field holds
// exactly one reference to the new profile and none to the old one. An apply
// that forgot to relinquish the load's lease would leave `first` resident with
// nothing naming it; one that forgot to release the replaced handle would do
// the same to `second` on the way back.
TEST(AssetFieldIo, DataFieldHoldsExactlyOneReferenceAcrossEdits)
{
    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets(logging, serializers);
    TempProfileFile first("first");
    TempProfileFile second("second");
    first.RegisterIn(assets.Registry);
    second.RegisterIn(assets.Registry);

    MovementProfileHandle field{};
    WriteProfile(assets.Assets, field, Value({ first.Virtual.c_str() }));
    WriteProfile(assets.Assets, field, Value({ second.Virtual.c_str() }));

    EXPECT_FALSE(assets.DataAssets.Find(first.Virtual).IsValid());
    ASSERT_TRUE(assets.DataAssets.Find(second.Virtual).IsValid());

    // Undo's direction, which the edit command replays through the same call.
    WriteProfile(assets.Assets, field, Value({ first.Virtual.c_str() }));
    EXPECT_FALSE(assets.DataAssets.Find(second.Virtual).IsValid());
    ASSERT_TRUE(assets.DataAssets.Find(first.Virtual).IsValid());

    WriteProfile(assets.Assets, field, AssetFieldValue{});
    EXPECT_FALSE(assets.DataAssets.Find(first.Virtual).IsValid());
}

// A profile that no longer resolves leaves the field unset rather than holding
// a handle to nothing -- and still releases what it replaced.
TEST(AssetFieldIo, DataFieldClearsWhenTheProfileIsGone)
{
    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets(logging, serializers);
    TempProfileFile profile("present");
    profile.RegisterIn(assets.Registry);

    MovementProfileHandle field{};
    WriteProfile(assets.Assets, field, Value({ profile.Virtual.c_str() }));
    ASSERT_TRUE(field.IsValid());

    WriteProfile(assets.Assets, field, Value({ "asset://data/missing.sdata" }));
    EXPECT_FALSE(field.IsValid());
    EXPECT_FALSE(assets.DataAssets.Find(profile.Virtual).IsValid());
}

// Relinquish hands the reference over rather than dropping it: the entry
// survives the lease's destruction, and is freed by the release that follows.
TEST(AssetFieldIo, RelinquishingALeaseKeepsTheReference)
{
    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets(logging, serializers);
    TempProfileFile profile("kept");
    profile.RegisterIn(assets.Registry);

    std::uint64_t token = 0;
    {
        AssetLease lease = assets.Assets.LoadLease(profile.Virtual, AssetType::Data);
        ASSERT_TRUE(lease.IsValid());
        token = lease.Relinquish();
        EXPECT_FALSE(lease.IsValid());
    }
    EXPECT_TRUE(assets.DataAssets.Find(profile.Virtual).IsValid());

    // Find took a reference of its own; drop both.
    assets.DataAssets.Release(assets.DataAssets.Find(profile.Virtual));
    assets.DataAssets.Release(assets.DataAssets.Find(profile.Virtual));
    assets.Assets.ReleaseLease(AssetType::Data, token);
    EXPECT_FALSE(assets.DataAssets.Find(profile.Virtual).IsValid());
}

// The description the inspector draws from: one asset field, narrowed to the
// subtype a movement profile declares. Without it the component is a bare
// header in the inspector, which is what this whole path exists to fix.
TEST(AssetFieldIo, MovementTuningDescribesItsProfileField)
{
    const std::unique_ptr<IComponentSerializer> serializer =
        MakeMovementTuningSourceSerializer();
    const std::span<const RuntimeField> fields = serializer->RuntimeFields();
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0].Name, "profile");
    EXPECT_EQ(fields[0].Offset, 0u);
    EXPECT_EQ(fields[0].Asset, AssetType::Data);
    EXPECT_EQ(fields[0].Arity, AssetArity::Single);
    EXPECT_EQ(fields[0].DataSubtype, kMovementProfileTypeName);
    EXPECT_FALSE(fields[0].Label.empty());
}
