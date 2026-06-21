#pragma once

#include <string>
#include <vector>

//=============================================================================
// ProjectDescriptor
//
// A Sencha project (.senchaproj): the unit the editor opens. It names the game
// module to load (for component serializers, so scenes carrying game components
// are editable) and the content roots, and it is what PIE launches `app`
// against. Editor-only: the runtime takes a module path and a content CWD on the
// command line, not a project file.
//
// Parsed from JSON. Relative paths in the file resolve against the descriptor's
// own directory, so a project is relocatable as a folder.
//=============================================================================
struct ProjectDescriptor
{
    std::string Name;
    std::string Directory;                  // absolute dir containing the .senchaproj
    std::string GameModulePath;             // absolute path to the built game module
    std::vector<std::string> ContentRoots;  // absolute content roots

    // Loads and resolves a descriptor. Returns false and sets *error on a missing
    // file, malformed JSON, or a missing required field.
    static bool Load(const std::string& path, ProjectDescriptor& out, std::string* error);

    // Writes the descriptor to `path` as JSON, with paths made relative to the
    // file's directory where possible so the project stays a relocatable folder.
    // Sets Directory to that directory. Returns false and sets *error on failure.
    bool Save(const std::string& path, std::string* error = nullptr);

    // Creates a new project: a descriptor with a default game module and content
    // root, written to <directory>/project.senchaproj, with the content root dir
    // created. Returns false and sets *error on failure.
    static bool Create(const std::string& directory,
                       const std::string& name,
                       ProjectDescriptor& out,
                       std::string* error);
};
