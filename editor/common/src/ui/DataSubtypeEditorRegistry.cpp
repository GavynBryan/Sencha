#include "ui/DataSubtypeEditorRegistry.h"

#include <utility>

bool DataSubtypeEditorRegistry::Register(std::unique_ptr<IDataSubtypeEditor> editor)
{
    if (editor == nullptr || editor->Subtype().empty())
        return false;
    if (Find(editor->Subtype()) != nullptr)
        return false;

    Editors.push_back(std::move(editor));
    return true;
}

IDataSubtypeEditor* DataSubtypeEditorRegistry::Find(std::string_view subtype) const
{
    for (const std::unique_ptr<IDataSubtypeEditor>& editor : Editors)
    {
        if (editor->Subtype() == subtype)
            return editor.get();
    }
    return nullptr;
}
