// AssetFieldIo: the editor's live-handle asset I/O. The List (per-slot material)
// path is the one that carries refcount risk, so it is exercised headlessly here
// against a real AssetSystem with material caches. The Single (static mesh) path
// needs a graphics-backed StaticMeshCache and is covered at runtime, not here.

#include "document/AssetFieldIo.h"

#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <core/logging/LoggingProvider.h>
#include <render/Material.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>

namespace
{
    // A sink-less logger plus the material caches, wired into an AssetSystem the
    // way the editor wires the real one (only the material half is needed here).
    struct AssetFieldFixture
    {
        LoggingProvider  Logging;
        AssetRegistry    Registry{ Logging };
        MaterialCache    Materials;
        MaterialSetCache Sets{ &Materials };
        AssetSystem      Assets{ Logging, Registry, nullptr, &Materials,
                                 nullptr, nullptr, nullptr, nullptr, nullptr, &Sets };

        MaterialHandle Register(const char* path)
        {
            return Assets.RegisterProceduralMaterial(path, Material{});
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

    f.Assets.ReleaseMaterialSet(field);
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
    f.Assets.ReleaseMaterial(hA);
    f.Assets.ReleaseMaterial(hB);
    ASSERT_NE(f.Materials.Get(hA), nullptr);
    ASSERT_NE(f.Materials.Get(hB), nullptr);

    WriteList(f.Assets, field, Value({ a, c }));

    EXPECT_NE(f.Materials.Get(hA), nullptr); // shared slot 0 survived the swap
    EXPECT_EQ(f.Materials.Get(hB), nullptr); // only the old set held b; it is gone

    const AssetFieldValue read = ReadList(f.Assets, field);
    ASSERT_EQ(read.Refs.size(), 2u);
    EXPECT_EQ(read.Refs[0].Path, a);
    EXPECT_EQ(read.Refs[1].Path, c);

    f.Assets.ReleaseMaterialSet(field);
}

// id-first resolution: a ref carrying the stable id but a stale path (the case an
// undo holds after the asset was renamed) resolves to the asset's current path.
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

    f.Assets.ReleaseMaterialSet(field);
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

    f.Assets.ReleaseMaterialSet(field);
}
