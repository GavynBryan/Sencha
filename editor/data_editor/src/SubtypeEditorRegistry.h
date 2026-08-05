#pragma once

#include "SubtypeEditor.h"

#include <memory>
#include <span>
#include <string_view>
#include <vector>

// The purpose-built editors this data editor knows about, keyed by subtype.
//
// The core asks this registry what to do with the active document and never
// names a subtype itself; a subtype nobody claims keeps the schema-generated
// form. Editors are registered once from a composition root rather than
// registering themselves, so what a build contains is readable in one file.
class SubtypeEditorRegistry
{
public:
    // Rejects a second editor for a subtype: two surfaces claiming one document
    // would both draw it and race for its platform events.
    bool Register(std::unique_ptr<IDataSubtypeEditor> editor);

    [[nodiscard]] IDataSubtypeEditor* Find(std::string_view subtype) const;

    [[nodiscard]] std::span<const std::unique_ptr<IDataSubtypeEditor>> Entries() const
    {
        return Editors;
    }

private:
    // A handful of entries scanned once per frame; an index would cost more to
    // maintain than it saves.
    std::vector<std::unique_ptr<IDataSubtypeEditor>> Editors;
};
