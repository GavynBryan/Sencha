#pragma once

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorScene.h"
#include "meshedit/MeshElements.h"
#include "meshedit/MeshElementKind.h"
#include "selection/SelectableRef.h"
#include "workspace/EditorWorkspace.h"

#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <vector>

// Boots the workspace aggregator over a one-zone world: the editing stack
// (sink, tool context, tools, dispatcher, manipulator session) is built against
// the focus document, with no window, renderer, or panel involved.
class WorkspaceTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override { Workspace.World.NewWorld("WorkspaceTest"); }

    [[nodiscard]] EditorScene& Scene() { return Workspace.ActiveDocument().GetScene(); }
    [[nodiscard]] RegistryId Registry() { return Workspace.ActiveDocument().GetRegistry().Id; }

    // A second open zone in the same graph, for the transitions that only exist
    // with more than one zone (focus change, unload). Focus stays where it was.
    [[nodiscard]] ZoneId AddSecondZone()
    {
        const std::vector<GraphRecord>& graphs = Workspace.World.Manifest().Graphs;
        EXPECT_FALSE(graphs.empty());
        if (graphs.empty())
            return {};
        const ZoneId zone = Workspace.World.AddZone(graphs.front().Id, "Second");
        EXPECT_TRUE(Workspace.World.LoadZone(zone));
        return zone;
    }

    [[nodiscard]] EntityId AddBrush(Vec3d position, Vec3d halfExtents = { 0.5, 0.5, 0.5 })
    {
        return Scene().CreateBrush(position, halfExtents);
    }

    // Element refs for one brush, in the mesh's own index order.
    [[nodiscard]] std::vector<SelectableRef> RefsOf(EntityId entity, MeshElementKind kind)
    {
        const BrushMesh* mesh = Scene().TryGetBrushMesh(entity);
        const Transform3f* transform = Scene().TryGetWorldTransform(entity);
        EXPECT_NE(mesh, nullptr);
        EXPECT_NE(transform, nullptr);
        if (mesh == nullptr || transform == nullptr)
            return {};
        return MeshElements::AllRefs(*mesh, *transform, Registry(), entity, kind);
    }

    void Select(std::vector<SelectableRef> refs)
    {
        Workspace.Selection.SetSelection(std::move(refs));
    }

    void SelectEntity(EntityId entity)
    {
        Select({ SelectableRef::EntitySelection(Registry(), entity) });
    }

    // Picks the named elements of one brush by mesh index.
    void SelectElements(EntityId entity, MeshElementKind kind, const std::vector<std::uint32_t>& indices)
    {
        const std::vector<SelectableRef> all = RefsOf(entity, kind);
        std::vector<SelectableRef> picked;
        picked.reserve(indices.size());
        for (const std::uint32_t index : indices)
        {
            ASSERT_LT(index, all.size());
            picked.push_back(all[index]);
        }
        Select(std::move(picked));
    }

    [[nodiscard]] std::size_t BrushCount()
    {
        std::size_t count = 0;
        for (const EntityId entity : Scene().GetAllEntities())
        {
            if (Scene().TryGetBrushMesh(entity) != nullptr)
                ++count;
        }
        return count;
    }

    LoggingProvider Logging;
    CommandStack Commands;
    EditorWorkspace Workspace{ Logging, Commands };
};
