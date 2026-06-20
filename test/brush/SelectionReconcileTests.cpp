#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace
{
EntityId Entity(std::uint32_t index)
{
    return EntityId{ index, 1 };
}
}

// After a structural mesh edit (extrude/delete/clip/split), element indices shift
// or reindex, so a kept element ref would resolve to the wrong element — or, if it
// happens to fall out of range, none. ClearMeshElementSelections drops per-element
// (vertex/edge/face) refs while keeping object-level (entity) selection.
TEST(SelectionReconcile, ClearsElementRefsKeepsObjectRefs)
{
    SelectionContext context;
    SelectionService service(context);

    const RegistryId registry = RegistryId::Global();
    const SelectableRef object = SelectableRef::EntitySelection(registry, Entity(1));
    const SelectableRef faceA = SelectableRef::FaceSelection(registry, Entity(1), 0);
    const SelectableRef faceB = SelectableRef::FaceSelection(registry, Entity(1), 2);

    service.SetSelection({ object, faceA, faceB });
    ASSERT_EQ(service.GetSelection().size(), 3u);

    service.ClearMeshElementSelections();

    const std::span<const SelectableRef> remaining = service.GetSelection();
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining.front(), object);
    // The primary must not be left pointing at a dropped (stale) element.
    EXPECT_FALSE(service.GetPrimarySelection().IsMeshElement());
}

// Mixed element kinds (vertex/edge/face) are all dropped, across entities.
TEST(SelectionReconcile, ClearsAllElementKinds)
{
    SelectionContext context;
    SelectionService service(context);

    const RegistryId registry = RegistryId::Global();
    service.SetSelection({
        SelectableRef::EntitySelection(registry, Entity(1)),
        SelectableRef::VertexSelection(registry, Entity(1), 4),
        SelectableRef::EdgeSelection(registry, Entity(1), 7),
        SelectableRef::FaceSelection(registry, Entity(2), 1),
    });

    service.ClearMeshElementSelections();

    const std::span<const SelectableRef> remaining = service.GetSelection();
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_TRUE(remaining.front().IsEntity());
}

// A selection with no element refs is left untouched.
TEST(SelectionReconcile, NoElementSelectionIsNoOp)
{
    SelectionContext context;
    SelectionService service(context);

    const SelectableRef object = SelectableRef::EntitySelection(RegistryId::Global(), Entity(5));
    service.SetSelection({ object });

    service.ClearMeshElementSelections();

    ASSERT_EQ(service.GetSelection().size(), 1u);
    EXPECT_EQ(service.GetSelection().front(), object);
}
