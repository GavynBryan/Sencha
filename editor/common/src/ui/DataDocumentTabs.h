#pragma once

#include "prompts/PromptQueue.h"

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
    DataDocumentTabs(DataDocumentSet& documents, PromptQueue& prompts,
                     const DataSubtypeEditorRegistry* editors = nullptr)
        : Documents(documents)
        , Editors(editors)
        , Prompts(prompts.MakeAsker())
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
    PromptQueue::Asker Prompts;
    std::string CloseError;
};
