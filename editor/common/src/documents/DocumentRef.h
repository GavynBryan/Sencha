#pragma once

#include <string>

class DocumentSource;

// Keys are local to their source; the same key in two sources names two documents.
struct DocumentRef
{
    DocumentSource* Source = nullptr;
    std::string Key;

    friend bool operator==(const DocumentRef&, const DocumentRef&) = default;
};
