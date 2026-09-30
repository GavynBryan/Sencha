#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

//=============================================================================
// DocumentLibrary
//
// The authored documents under the roots a previewer mounts, grouped by which
// root they came from: a project's own UI, the engine's shell defaults, an
// editor's surfaces. A row per `.rml`, with the model name its markup declares
// and whether a cooked package for it exists beside it.
//
// A scan, not a parse: the model name is read the way the cooker reads a
// stylesheet link, by looking for the attribute. Nothing here interprets a
// document; that is the layer's job once one is opened.
//=============================================================================
struct DocumentEntry
{
    // The library the root was added as: "Project", "Engine", "Editor".
    std::string Library;
    std::string Root;
    // Root-relative, forward slashes: "ui/pause.rml".
    std::string RelPath;
    // The virtual path a host opens it by. The cooker keeps the source
    // extension: "asset://ui/pause.rml".
    std::string PackagePath;
    // What `data-model="..."` names, or empty for a static document.
    std::string ModelName;
    // Whether `<root>/.cooked/<rel>.sui` exists.
    bool Cooked = false;
    // Whether a `.preview.json` sits beside the source.
    bool HasPreviewModel = false;

    [[nodiscard]] std::filesystem::path SourcePath() const;
};

class DocumentLibrary
{
public:
    void AddRoot(std::string library, std::string root);
    // Walks every root again. Documents are ordered by library, then path.
    void Rescan();

    [[nodiscard]] const std::vector<DocumentEntry>& Documents() const { return Entries; }
    struct LibraryRoot
    {
        std::string Library;
        std::string Path;
    };
    // Every root added, whether or not a document was found under it: a root
    // is watched for what may be created there, not for what is there now.
    [[nodiscard]] const std::vector<LibraryRoot>& LibraryRoots() const { return Roots; }
    [[nodiscard]] std::size_t RootCount() const { return Roots.size(); }
    [[nodiscard]] const DocumentEntry* Find(std::string_view packagePath) const;

private:
    std::vector<LibraryRoot> Roots;
    std::vector<DocumentEntry> Entries;
};

// The value of the first `data-model="..."` attribute in `markup`, or empty.
[[nodiscard]] std::string ScanDataModelName(std::string_view markup);
