#pragma once

#include "ui/DataSubtypeEditor.h"

#include <memory>
#include <span>
#include <string_view>
#include <vector>

// The purpose-built subtype editors, registered once from a composition root.
class DataSubtypeEditorRegistry
{
public:
    // Rejects a second editor for a subtype, which would race the first for its events.
    bool Register(std::unique_ptr<IDataSubtypeEditor> editor);

    [[nodiscard]] IDataSubtypeEditor* Find(std::string_view subtype) const;

    [[nodiscard]] std::span<const std::unique_ptr<IDataSubtypeEditor>> Entries() const
    {
        return Editors;
    }

private:
    std::vector<std::unique_ptr<IDataSubtypeEditor>> Editors;
};
