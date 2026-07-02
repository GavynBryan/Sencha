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
    // Grid-origin edit wins over everything, then pivot edit.
    EXPECT_EQ(NextEscapeAction(true, true, true, MeshElementKind::Face, true),
              EscapeAction::CancelGridOriginEdit);
    EXPECT_EQ(NextEscapeAction(false, true, true, MeshElementKind::Face, true),
              EscapeAction::CancelPivotEdit);
    // Element refs clear before the mode drops.
    EXPECT_EQ(NextEscapeAction(false, false, true, MeshElementKind::Face, true),
              EscapeAction::ClearElementSelection);
    // Mode drops before the entity selection clears.
    EXPECT_EQ(NextEscapeAction(false, false, false, MeshElementKind::Face, true),
              EscapeAction::DropToObjectMode);
    EXPECT_EQ(NextEscapeAction(false, false, false, MeshElementKind::Object, true),
              EscapeAction::ClearSelection);
    EXPECT_EQ(NextEscapeAction(false, false, false, MeshElementKind::Object, false),
              EscapeAction::None);
}

TEST(EscapePolicy, ModeDropsEvenWithoutSelection)
{
    EXPECT_EQ(NextEscapeAction(false, false, false, MeshElementKind::Vertex, false),
              EscapeAction::DropToObjectMode);
}
