#pragma once

#include <optional>
#include <string>

// Resolves the project descriptor (.senchaproj) path for an editor
// application: "--project <path>" on the command line wins, then the
// SENCHA_PROJECT environment variable. Returns nullopt when neither is set.
std::optional<std::string> ResolveProjectPath(int argc, char** argv);
