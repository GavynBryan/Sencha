#include <ecs/World.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <vector>

// The data path behind the editor's RawComponentEditCommand: snapshot a
// component's raw bytes, mutate by id, and restore — all without naming the type
// at the edit site (the editor learns game components only by ComponentTypeId).

struct EditTarget { float A = 0.f; int B = 0; };
SENCHA_DECLARE_COMPONENT_TYPE(EditTarget, "test.edit_target");

namespace
{
    std::vector<std::byte> Snapshot(const World& w, EntityId e, ComponentId id, std::size_t size)
    {
        const void* src = w.GetComponentRaw(e, id);
        std::vector<std::byte> bytes(size);
        std::memcpy(bytes.data(), src, size);
        return bytes;
    }

    void ApplyRaw(World& w, EntityId e, ComponentId id, const std::vector<std::byte>& bytes)
    {
        void* dst = w.GetComponentRaw(e, id);
        std::memcpy(dst, bytes.data(), bytes.size());
    }
}

TEST(TypeErasedComponentEdit, RawAccessRoundTripsByComponentId)
{
    World w;
    w.RegisterComponent<EditTarget>();
    const EntityId e = w.CreateEntity();
    w.AddComponent<EditTarget>(e, { 1.5f, 7 });

    // The id is resolved from the stable identity, not the C++ type name.
    const ComponentId id = w.GetComponentIdByType(MakeComponentTypeId("test.edit_target"));
    ASSERT_NE(id, InvalidComponentId);
    ASSERT_NE(w.GetComponentRaw(e, id), nullptr);

    auto* typed = static_cast<const EditTarget*>(w.GetComponentRaw(e, id));
    EXPECT_FLOAT_EQ(typed->A, 1.5f);
    EXPECT_EQ(typed->B, 7);
}

TEST(TypeErasedComponentEdit, SnapshotMutateRestoreMatchesUndoRedo)
{
    World w;
    w.RegisterComponent<EditTarget>();
    const EntityId e = w.CreateEntity();
    w.AddComponent<EditTarget>(e, { 1.5f, 7 });

    const ComponentId id   = w.GetComponentId<EditTarget>();
    const std::size_t size = w.GetMeta(id)->Size;

    // before = current; after = an edited copy (as the inspector would produce).
    const std::vector<std::byte> before = Snapshot(w, e, id, size);
    EditTarget edited{ 9.0f, 42 };
    std::vector<std::byte> after(size);
    std::memcpy(after.data(), &edited, size);

    // Execute (apply after)
    ApplyRaw(w, e, id, after);
    EXPECT_FLOAT_EQ(w.TryGet<EditTarget>(e)->A, 9.0f);
    EXPECT_EQ(w.TryGet<EditTarget>(e)->B, 42);

    // Undo (apply before)
    ApplyRaw(w, e, id, before);
    EXPECT_FLOAT_EQ(w.TryGet<EditTarget>(e)->A, 1.5f);
    EXPECT_EQ(w.TryGet<EditTarget>(e)->B, 7);

    // Redo (apply after again)
    ApplyRaw(w, e, id, after);
    EXPECT_EQ(w.TryGet<EditTarget>(e)->B, 42);
}

TEST(TypeErasedComponentEdit, RawAccessIsNullForAbsentComponent)
{
    World w;
    w.RegisterComponent<EditTarget>();
    const EntityId e = w.CreateEntity(); // no component added
    EXPECT_EQ(w.GetComponentRaw(e, w.GetComponentId<EditTarget>()), nullptr);
}
