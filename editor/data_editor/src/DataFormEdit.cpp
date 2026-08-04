#include "DataFormEdit.h"

#include "DataDocument.h"
#include "DataEditorWorkspace.h"

#include <utility>

void ApplyFieldEdit(DataDocument& document,
                    DataEditorWorkspace& workspace,
                    const FieldEdit& edit,
                    JsonValue root)
{
    if (edit.Changed)
    {
        document.BeginEdit();
        document.PreviewRoot(std::move(root));
        workspace.ValidateActive();
    }

    // A commit with no change still closes a scope an earlier frame opened, so
    // it runs whether or not this frame moved anything.
    if (edit.Committed)
        document.CommitEdit();
}
