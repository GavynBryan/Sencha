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
};
