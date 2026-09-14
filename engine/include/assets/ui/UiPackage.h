#pragma once

#include <core/assets/AssetRef.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// UiPackage
//
// One authored UI document and everything needed to reconstruct it without
// touching the filesystem: the root markup, every included markup and
// stylesheet blob, the resource table naming the fonts and textures it
// depends on, and what the cooker noticed it could not promise to draw.
//
// The runtime form of a .sui (docs/ui/architecture.md). Plain CPU data: no
// handles, no leases, no RmlUi type. The package cache owns exactly this and
// nothing else -- an open screen owns the asset leases, because document
// lifetime and resource lifetime have to line up and the cache outlives both.
//=============================================================================

enum class UiBlobKind : std::uint16_t
{
    Document = 0,   // markup: the root, or one pulled in by it
    StyleSheet = 1, // RCSS
};

// One file as the document engine will ask for it.
//
// VirtualName is the name the markup uses to refer to it -- an `@import` target
// or a `<link href>` -- normalised by the cooker so a document's own spelling
// resolves. SourcePath is where it came from on disk, carried only so a parse
// error can name a file a human recognises.
struct UiPackageBlob
{
    std::string VirtualName;
    std::string SourcePath;
    UiBlobKind Kind = UiBlobKind::Document;
    std::vector<std::byte> Bytes;
};

// A rendering capability the authored source asked for that the supported
// profile does not cover (docs/ui/architecture.md §7). Recorded at cook time so
// an author hears about it before the frame it would have drawn wrong.
//
// Advisory, deliberately. The runtime adapter diagnoses an unsupported
// operation that actually reaches it, and that is the authority; static
// detection here would have to reimplement the cascade to be exhaustive.
struct UiUnsupportedFeature
{
    std::string Feature;     // the RCSS property or construct, e.g. "box-shadow"
    std::string SourcePath;
    std::uint32_t Line = 0;
};

struct UiPackage
{
    // Index 0 of Blobs by construction; named separately so a reader does not
    // have to know that.
    std::string RootDocumentName;

    std::vector<UiPackageBlob> Blobs;

    // Fonts and textures this document needs. The stage half hands these to the
    // asset system as dependencies, so the preloader warms them before the
    // document opens; the loaded screen then resolves against this table rather
    // than discovering assets at render time.
    std::vector<AssetRef> Resources;

    std::vector<UiUnsupportedFeature> Unsupported;

    [[nodiscard]] bool IsValid() const
    {
        return !RootDocumentName.empty() && !Blobs.empty();
    }

    // The blob a given virtual name resolves to, or null. Linear: a package
    // holds a handful of blobs and this runs at document construction, not per
    // frame.
    //
    // Both sides are normalised, so a document engine that asks for "./x.rcss"
    // or "/x.rcss" finds the blob the cooker stored as "x.rcss". Being forgiving
    // in one place beats being exactly right in two that have to agree.
    [[nodiscard]] const UiPackageBlob* FindBlob(std::string_view virtualName) const;
};

// The canonical spelling of a blob name: lexically normalised, forward slashes,
// no leading "/" or "./". The cooker stores names in this form and lookups
// convert to it.
[[nodiscard]] std::string NormalizeUiBlobName(std::string_view name);
