#pragma once

#include <core/json/JsonValue.h>

class DataDocument;
class DataEditorWorkspace;

// What a widget did to the working value this frame.
//
// The split exists because a drag reports a change on every frame it moves but
// should leave one undo entry behind. Continuous widgets (drags, text fields)
// report Changed while the interaction runs and Committed when it ends;
// instantaneous ones (checkboxes, combo picks, structural buttons) report both
// at once and so land immediately.
struct FieldEdit
{
    bool Changed = false;
    bool Committed = false;

    FieldEdit& operator|=(const FieldEdit& other)
    {
        Changed = Changed || other.Changed;
        Committed = Committed || other.Committed;
        return *this;
    }

    // A change that has no interaction to outlive it.
    [[nodiscard]] static FieldEdit Instant() { return FieldEdit{ true, true }; }
};

// Routes one frame's edit into the document's transaction: opens a scope on the
// first change, previews while the interaction runs, and commits when it ends.
// Forms call this instead of driving Begin/Preview/Commit themselves so every
// authoring surface coalesces the same way.
void ApplyFieldEdit(DataDocument& document,
                    DataEditorWorkspace& workspace,
                    const FieldEdit& edit,
                    JsonValue root);
