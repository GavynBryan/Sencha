#include "editmodes/TransformModeMemory.h"
#include "workspace/EscapePolicy.h"

#include <gtest/gtest.h>

TEST(TransformModeMemory, DefaultsMatchContexts)
{
    const TransformModeMemory memory;
    EXPECT_EQ(memory.ModeFor(false), TransformMode::Resize);
    EXPECT_EQ(memory.ModeFor(true), TransformMode::Move);
}

TEST(TransformModeMemory, RecordsPerContext)
{
    TransformModeMemory memory;
    memory.Record(false, TransformMode::Rotate);
    memory.Record(true, TransformMode::Scale);
    EXPECT_EQ(memory.ModeFor(false), TransformMode::Rotate);
    EXPECT_EQ(memory.ModeFor(true), TransformMode::Scale);
}

TEST(TransformModeMemory, ResizeIsNeverRecordedForElementContext)
{
    TransformModeMemory memory;
    memory.Record(true, TransformMode::Rotate);
    memory.Record(true, TransformMode::Resize);
    EXPECT_EQ(memory.ModeFor(true), TransformMode::Rotate);

    memory.Record(false, TransformMode::Resize);
    EXPECT_EQ(memory.ModeFor(false), TransformMode::Resize);
}

TEST(EscapePolicy, ClimbsOneLevelPerPress)
{
    // A staged preview is the outermost context: it goes first, before any
    // selection or mode state is touched.
    EXPECT_EQ(NextEscapeAction({ .HasPendingEdit = true,
                                 .GridOriginEditing = true,
                                 .PivotEditing = true,
                                 .HasElementRefs = true,
                                 .ElementKind = MeshElementKind::Face,
                                 .HasSelection = true }),
              EscapeAction::CancelPendingEdit);
    // Then grid-origin edit, then pivot edit.
    EXPECT_EQ(NextEscapeAction({ .GridOriginEditing = true,
                                 .PivotEditing = true,
                                 .HasElementRefs = true,
                                 .ElementKind = MeshElementKind::Face,
                                 .HasSelection = true }),
              EscapeAction::CancelGridOriginEdit);
    EXPECT_EQ(NextEscapeAction({ .PivotEditing = true,
                                 .HasElementRefs = true,
                                 .ElementKind = MeshElementKind::Face,
                                 .HasSelection = true }),
              EscapeAction::CancelPivotEdit);
    // Element refs clear before the mode drops.
    EXPECT_EQ(NextEscapeAction({ .HasElementRefs = true,
                                 .ElementKind = MeshElementKind::Face,
                                 .HasSelection = true }),
              EscapeAction::ClearElementSelection);
    // Mode drops before the entity selection clears.
    EXPECT_EQ(NextEscapeAction({ .ElementKind = MeshElementKind::Face, .HasSelection = true }),
              EscapeAction::DropToObjectMode);
    EXPECT_EQ(NextEscapeAction({ .ElementKind = MeshElementKind::Object, .HasSelection = true }),
              EscapeAction::ClearSelection);
    EXPECT_EQ(NextEscapeAction({ .ElementKind = MeshElementKind::Object, .HasSelection = false }),
              EscapeAction::None);
}

TEST(EscapePolicy, ModeDropsEvenWithoutSelection)
{
    EXPECT_EQ(NextEscapeAction({ .ElementKind = MeshElementKind::Vertex, .HasSelection = false }),
              EscapeAction::DropToObjectMode);
}
