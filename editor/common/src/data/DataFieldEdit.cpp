#include "data/DataFieldEdit.h"

#include "data/DataDocument.h"

#include <core/metadata/DataSchema.h>

#include <utility>

std::string DataFieldDisplayName(const DataFieldSchema& field)
{
    return field.DisplayName.empty() ? field.Key : field.DisplayName;
}

void ApplyFieldEdit(DataDocument& document, DataFormHost& host, const FieldEdit& edit, JsonValue root)
{
    if (edit.Changed)
    {
        document.BeginEdit();
        document.PreviewRoot(std::move(root));
        host.EditPreviewed(document);
    }

    // A commit with no change still closes a scope an earlier frame opened, so
    // it runs whether or not this frame moved anything.
    if (edit.Committed)
    {
        document.CommitEdit();
        host.EditCommitted(document);
    }
}
