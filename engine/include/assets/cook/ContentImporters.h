#pragma once

#include <assets/cook/AssetImporter.h>
#include <assets/cook/BlendCook.h>
#include <assets/cook/FontCook.h>
#include <assets/cook/MeshCook.h>
#include <assets/cook/TextureCook.h>
#include <assets/cook/UiPackageCook.h>

class JobSystem;

//=============================================================================
// ContentImporterSet. Dev-only, compiled under SENCHA_ENABLE_COOK.
//
// Every source importer the engine ships, owned and registered together.
//
// AssetImporterRegistry is non-owning, so each call site used to declare the
// importers itself and register them one by one -- which meant a new source
// asset type cost an identical edit in three places, and the cost of forgetting
// one was that sources of that type silently never cooked in whichever tool was
// missed.
//
// Not every site wants the whole set, and that stays true: a cook step that
// deliberately handles one kind should keep declaring that one importer, with
// the comment saying why. This is for the sites that mean "everything".
//=============================================================================
class ContentImporterSet
{
public:
    // `jobs` is handed to the importers that can parallelise their own work
    // (texture compression). Null is valid and means single-threaded.
    explicit ContentImporterSet(JobSystem* jobs = nullptr);

    [[nodiscard]] AssetImporterRegistry& Registry() { return Importers; }
    [[nodiscard]] const AssetImporterRegistry& Registry() const { return Importers; }

private:
    PngTextureImporter Texture;
    GltfMeshImporter Gltf;
    BlendMeshImporter Blend;
    FontFaceImporter Font;
    UiPackageImporter UiPackage;

    // Declared last: it borrows the importers above, so it must not outlive them.
    AssetImporterRegistry Importers;
};
