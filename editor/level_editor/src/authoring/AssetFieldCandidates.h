#pragma once

#include <core/assets/AssetId.h>

#include <string>
#include <vector>

class AssetRegistry;
class AssetSystem;
struct RuntimeField;

//=============================================================================
// What a picker for an asset-handle field is allowed to offer.
//
// Shared by the panel and the authored surface rather than duplicated, because
// the narrowing is the interesting part: a structured-data field accepts one
// subtype, and working that out reads each candidate's envelope off disk. Doing
// it twice, differently, would mean two answers to what a field accepts.
//=============================================================================
struct AssetFieldCandidate
{
    std::string Path;
    AssetId     Id;
};

// Stable and sorted, because AssetRegistry::Records() is unordered and a picker
// whose entries move between openings is unusable.
//
// Narrowed to the subtype a Data field names; a field that names none takes any
// data asset, and a field of any other kind has nothing to narrow by. Scanning
// and the per-candidate envelope read are why this is called when a picker
// opens rather than every frame one is on screen.
[[nodiscard]] std::vector<AssetFieldCandidate> FindAssetFieldCandidates(
    const AssetRegistry& catalog, AssetSystem& assets, const RuntimeField& field);
