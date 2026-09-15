#pragma once

#include <assets/ui/UiPackage.h>

#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// UiSourceScan. Dev-only, compiled under SENCHA_ENABLE_COOK.
//
// What one authored UI source file refers to: the stylesheets it pulls in, the
// fonts and images it names, and the constructs it uses that the supported
// rendering profile does not cover.
//
// Deliberately a scanner and not a parser. Resolving this properly would mean
// reimplementing the cascade, and there is already something that does that
// correctly -- the document engine itself, at load. So the division of labour
// is: this finds what the cook needs to *package*, and the runtime adapter is
// the authority on what can actually be *drawn* (docs/ui/architecture.md §7).
//
// The consequences of that are worth stating plainly, because they are the
// limits of what this can promise:
//   - A reference assembled at runtime rather than written literally is not
//     found here. It will fail to resolve at load, with a diagnostic.
//   - An unsupported construct reported here is advisory. One that is missed
//     still fails loudly when it reaches the renderer.
//   - Comments are stripped first, so a commented-out @import does not pull a
//     file into the package.
//=============================================================================

struct UiSourceReferences
{
    // Stylesheet paths, exactly as written in the source. Resolving them
    // against the referring file is the caller's job -- it is the one that
    // knows where that file lives.
    std::vector<std::string> StyleSheets;

    // Fonts and textures, as written. Same resolution caveat.
    std::vector<AssetRef> Resources;

    std::vector<UiUnsupportedFeature> Unsupported;
};

// Scans one source. `sourcePath` is used only to attribute diagnostics.
// Appends to `out`, so a caller can accumulate across a document and its
// stylesheets without merging afterwards.
void ScanUiSource(std::string_view text,
                  UiBlobKind kind,
                  std::string_view sourcePath,
                  UiSourceReferences& out);
