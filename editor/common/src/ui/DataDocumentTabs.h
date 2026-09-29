#pragma once

#include "ui/DocumentShellActions.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

class DataDocument;
class DataDocumentSet;
class DataSubtypeEditorRegistry;

// The open data documents as closable tabs, each drawn through its subtype's
// editor or the schema form. A tab click goes through the set's activation,
// which commits an open edit before the new document's widgets are drawn.
class DataDocumentTabs
{
public:
    explicit DataDocumentTabs(DataDocumentSet& documents, const DataSubtypeEditorRegistry* editors = nullptr)
        : Documents(documents)
        , Editors(editors)
    {
    }

    // `header` draws above the active document's form.
    void Draw(const std::function<void(DataDocument&)>& header = {});

private:
    void DrawForm(DataDocument& document);

    DataDocumentSet& Documents;
    const DataSubtypeEditorRegistry* Editors = nullptr;
    // The active index the tab bar last showed; a difference means the set moved it.
    std::optional<std::size_t> Shown;
    UnsavedDocumentPrompt Prompt;
    std::string CloseError;
};
