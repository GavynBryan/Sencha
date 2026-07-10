#include "brush/BrushOps.h"
#include "meshedit/MeshElements.h"
#include "meshedit/SelectionConversion.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
const RegistryId kRegistry = RegistryId::Global();
const EntityId kEntity{ 3, 1 };

bool AllOfKind(const std::vector<SelectableRef>& refs, SelectableKind kind)
{
    return std::all_of(refs.begin(), refs.end(),
                       [&](const SelectableRef& r) { return r.Kind == kind; });
}
}

TEST(SelectionConversion, FaceToVerticesYieldsItsCorners)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const SelectableRef face = SelectableRef::FaceSelection(kRegistry, kEntity, 0);

    const auto verts = ConvertElementRefs(box, std::span(&face, 1), MeshElementKind::Vertex);
    ASSERT_EQ(verts.size(), 4u);
    EXPECT_TRUE(AllOfKind(verts, SelectableKind::Vertex));
    for (const SelectableRef& ref : verts)
        EXPECT_NE(std::find(box.Faces[0].Loop.begin(), box.Faces[0].Loop.end(), ref.ElementId),
                  box.Faces[0].Loop.end());
}

TEST(SelectionConversion, FaceToEdgesYieldsItsBoundary)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const SelectableRef face = SelectableRef::FaceSelection(kRegistry, kEntity, 0);

    const auto edges = ConvertElementRefs(box, std::span(&face, 1), MeshElementKind::Edge);
    EXPECT_EQ(edges.size(), 4u);
    EXPECT_TRUE(AllOfKind(edges, SelectableKind::Edge));
}

TEST(SelectionConversion, EdgeToFacesYieldsBothSides)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const SelectableRef edge = SelectableRef::EdgeSelection(kRegistry, kEntity, 0);

    const auto faces = ConvertElementRefs(box, std::span(&edge, 1), MeshElementKind::Face);
    EXPECT_EQ(faces.size(), 2u); // every closed-box edge borders exactly two faces
    EXPECT_TRUE(AllOfKind(faces, SelectableKind::Face));
}

TEST(SelectionConversion, VertexToEdgesYieldsIncidentEdges)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const SelectableRef vertex = SelectableRef::VertexSelection(kRegistry, kEntity, 0);

    const auto edges = ConvertElementRefs(box, std::span(&vertex, 1), MeshElementKind::Edge);
    EXPECT_EQ(edges.size(), 3u); // box corner valence
    EXPECT_TRUE(AllOfKind(edges, SelectableKind::Edge));
}

TEST(SelectionConversion, ToObjectCollapsesToOneEntityRef)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::vector<SelectableRef> refs = {
        SelectableRef::FaceSelection(kRegistry, kEntity, 0),
        SelectableRef::FaceSelection(kRegistry, kEntity, 1),
        SelectableRef::VertexSelection(kRegistry, kEntity, 2),
    };

    const auto out = ConvertElementRefs(box, refs, MeshElementKind::Object);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_TRUE(out[0].IsEntity());
    EXPECT_EQ(out[0].Entity, kEntity);
}

TEST(SelectionConversion, MatchingKindAndSharedElementsPassThroughDeduplicated)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::vector<SelectableRef> refs = {
        SelectableRef::FaceSelection(kRegistry, kEntity, 0),
        SelectableRef::FaceSelection(kRegistry, kEntity, 0),
    };

    const auto out = ConvertElementRefs(box, refs, MeshElementKind::Face);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].ElementId, 0u);

    // Two adjacent faces share corners: converting both to vertices dedups them.
    const std::vector<SelectableRef> adjacent = {
        SelectableRef::FaceSelection(kRegistry, kEntity, 0),
        SelectableRef::FaceSelection(kRegistry, kEntity, 1),
    };
    const auto verts = ConvertElementRefs(box, adjacent, MeshElementKind::Vertex);
    EXPECT_LE(verts.size(), 8u);
    EXPECT_GE(verts.size(), 6u);
}

TEST(GroupSelectionByEntity, SplitsRefsByEntityInFirstAppearanceOrder)
{
    const EntityId a{ 1, 1 };
    const EntityId b{ 2, 1 };
    SelectionSnapshot selection;
    selection.Items = {
        SelectableRef::FaceSelection(kRegistry, a, 0),
        SelectableRef::FaceSelection(kRegistry, b, 2),
        SelectableRef::FaceSelection(kRegistry, a, 1),
    };
    selection.Primary = selection.Items.front();

    const auto groups = GroupSelectionByEntity(selection, SelectableKind::Face);
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].Entity, a);
    ASSERT_EQ(groups[0].Refs.size(), 2u);
    EXPECT_EQ(groups[0].Refs[0].ElementId, 0u);
    EXPECT_EQ(groups[0].Refs[1].ElementId, 1u);
    EXPECT_EQ(groups[1].Entity, b);
    ASSERT_EQ(groups[1].Refs.size(), 1u);
}

TEST(GroupSelectionByEntity, PrimaryEntityGroupLeads)
{
    const EntityId a{ 1, 1 };
    const EntityId b{ 2, 1 };
    SelectionSnapshot selection;
    selection.Items = {
        SelectableRef::FaceSelection(kRegistry, a, 0),
        SelectableRef::FaceSelection(kRegistry, b, 2),
    };
    selection.Primary = selection.Items[1]; // b is primary but appears second

    const auto groups = GroupSelectionByEntity(selection, SelectableKind::Face);
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].Entity, b);
    EXPECT_EQ(groups[1].Entity, a);
}

TEST(GroupSelectionByEntity, FiltersOtherKindsAndInvalidRefs)
{
    const EntityId a{ 1, 1 };
    SelectionSnapshot selection;
    selection.Items = {
        SelectableRef::FaceSelection(kRegistry, a, 0),
        SelectableRef::EdgeSelection(kRegistry, a, 3),
        SelectableRef{},
        SelectableRef::EntitySelection(kRegistry, a),
    };
    selection.Primary = SelectableRef::EdgeSelection(kRegistry, a, 3);

    const auto faces = GroupSelectionByEntity(selection, SelectableKind::Face);
    ASSERT_EQ(faces.size(), 1u);
    EXPECT_EQ(faces[0].Refs.size(), 1u);

    // A primary of a different kind must not seed an empty group.
    const auto edges = GroupSelectionByEntity(selection, SelectableKind::Edge);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_EQ(edges[0].Refs.size(), 1u);
    EXPECT_EQ(edges[0].Refs[0].ElementId, 3u);

    SelectionSnapshot none;
    none.Primary = SelectableRef::FaceSelection(kRegistry, a, 0); // primary not in Items
    EXPECT_TRUE(GroupSelectionByEntity(none, SelectableKind::Face).empty());
}
