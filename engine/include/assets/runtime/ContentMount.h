#pragma once

#include <filesystem>
#include <string_view>

class Logger;
struct RuntimeAssets;

// Mounting a content root into an asset stack: what a runtime host, the scene
// viewer, and the editor each did by hand with the same four calls in the same
// order.
//
// A content root is an authored directory with a `.cooked` sibling holding the
// cook cache. Mounting it is two steps because the editor needs to work between
// them: it imports source assets on demand after the scan has classified them
// and before the cooked overlay is registered, so a dropped .png or .blend is
// cooked and then resolves through the cooked index like any other artifact.
//
// These functions fill an AssetRegistry and nothing else. They do not compose
// the stack, publish a world resource, or decide which roots exist -- the
// caller owns all three.
struct ContentRootPaths
{
    // The authored directory, as given.
    std::filesystem::path Authored;
    // Its cook cache, `Authored/.cooked`. Scanned for the artifacts whose
    // location keys them; the index inside it is read by RegisterCookedContent.
    std::filesystem::path Cooked;
};

[[nodiscard]] ContentRootPaths ResolveContentRoot(const std::filesystem::path& root);

// Registers every file under the root and its cook cache whose extension a
// registered kind claims. Cooked artifacts scanned here win over authored ones
// by being registered second.
void ScanContentRoot(const ContentRootPaths& root, RuntimeAssets& assets);

// Registers the cooked index (artifacts the physical scan cannot key, notably a
// cooked texture still serving its source virtual path) and applies the root's
// asset-id map. A root with no id map resolves by path only, which is a warning
// rather than a failure: a project that has never cooked has no map yet.
void RegisterCookedContent(const ContentRootPaths& root,
                           RuntimeAssets& assets,
                           Logger& log);

// Both steps in order, for a caller with nothing to do between them.
void MountContentRoot(const ContentRootPaths& root,
                      RuntimeAssets& assets,
                      Logger& log);
