#pragma once

#include "properties/PropertySource.h"

#include <functional>
#include <string>

// Where a document lives, as the one property that moves it: its path inside
// its content root, which, changed, moves the file.
class FilePathProperty final : public CommitOnlyPropertySource
{
public:
    // `path` is the document's asset path ("asset://..."), empty for none;
    // `move` is handed the asset path it is to move to, its extension kept
    // when the person leaves it off.
    FilePathProperty(std::function<std::string()> path, std::function<void(const std::string&)> move)
        : Path(std::move(path))
        , Move(std::move(move))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;

private:
    std::function<std::string()> Path;
    std::function<void(const std::string&)> Move;
};
