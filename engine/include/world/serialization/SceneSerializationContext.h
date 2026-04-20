#pragma once

class AssetSystem;
class MaterialCache;
class MeshCache;

//=============================================================================
// SceneSerializationContext
//
// Explicit dependencies used by scene field codecs to convert stable persisted
// references into runtime handles, and runtime handles back into stable refs.
//=============================================================================
struct SceneSerializationContext
{
    AssetSystem* Assets = nullptr;

    MeshCache* Meshes = nullptr;
    MaterialCache* Materials = nullptr;
};
