#pragma once

#include <string>
#include <string_view>

class IAssetSource;
struct AssetRecord;

//=============================================================================
// PeekDataAssetSubtype
//
// The subtype an .sdata envelope declares, read without loading, validating,
// compiling, or making the asset resident. Empty when the text is not a data
// asset envelope.
//
// This exists so an authoring surface can narrow a picker to the subtype a
// field accepts. The full load answers the same question, but it also needs
// the subtype registered, the schema satisfied, and a cache to commit into --
// none of which a picker offering an asset it has not chosen yet can assume.
//=============================================================================
[[nodiscard]] std::string PeekDataAssetSubtype(std::string_view json);

// The same question against a registered asset, resolving its bytes through
// the source. Empty when the bytes cannot be read.
[[nodiscard]] std::string PeekDataAssetSubtype(IAssetSource& source,
                                               const AssetRecord& record);
