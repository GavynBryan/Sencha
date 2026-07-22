#pragma once

#include "CookArtifactTransaction.h"
#include "CookReceipt.h"
#include "CookStepCache.h"

#include <assets/cook/CookedCache.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

// Owns every artifact known during one document cook and the invariant that a
// scene-referenced output, its public record, and its staged-vs-authored
// classification agree. Replaces the loose artifact vector, generated-path set,
// material-ref list, and the artifacts.back() idiom. It records membership only;
// it writes no files and decides no output policy.
class DocumentArtifactCatalog
{
public:
    // A generated cell mesh: staged under .cooked, referenced by its cell entity
    // (not an extra scene ref, since the entity names it directly).
    void AddMesh(std::string assetPath, std::string relPath)
    {
        Generated_.insert(assetPath);
        Artifacts_.push_back(CookedArtifact{
            std::move(assetPath), std::move(relPath), AssetType::StaticMesh });
    }

    // A cooked collision blob: staged, not scene-referenced.
    void AddCollision(std::string assetPath, std::string relPath)
    {
        Artifacts_.push_back(CookedArtifact{
            std::move(assetPath), std::move(relPath), AssetType::Collision });
    }

    // A distinct authored material the assembled scene references.
    void AddMaterial(const std::string& path)
    {
        if (SeenMaterial_.insert(path).second)
            SceneRefs_.push_back(path);
    }

    // A generated texture the scene references (lightmap atlas, AO plane): staged
    // and added to the scene's extra refs. Returns the recorded artifact.
    [[nodiscard]] CookedArtifact AddSceneTexture(std::string assetPath, std::string relPath)
    {
        Generated_.insert(assetPath);
        SceneRefs_.push_back(assetPath);
        Artifacts_.push_back(CookedArtifact{
            std::move(assetPath), std::move(relPath), AssetType::Texture });
        return Artifacts_.back();
    }

    // A generated probe volume: staged, located by path convention (not a scene
    // ref). Returns the recorded artifact.
    [[nodiscard]] CookedArtifact AddProbeVolume(std::string assetPath, std::string relPath)
    {
        Artifacts_.push_back(CookedArtifact{
            std::move(assetPath), std::move(relPath), AssetType::ProbeVolume });
        return Artifacts_.back();
    }

    // Pull a prior artifact forward unchanged (preserved collision on a cook that
    // does not refresh it). Not generated and not scene-referenced this cook.
    void AddExisting(const CookedArtifact& artifact) { Artifacts_.push_back(artifact); }

    // Restore a cached step's artifacts through the transaction, returning the
    // primary (last) restored artifact. `sceneReferenced` re-enters them into the
    // generated set and scene refs (direct/AO); false leaves them path-located
    // only (probes).
    [[nodiscard]] bool RestoreStep(const CookStepReceipt& receipt,
                                   const std::filesystem::path& assetsRoot,
                                   CookArtifactTransaction& transaction,
                                   bool sceneReferenced,
                                   CookedArtifact& primary,
                                   std::string* error)
    {
        if (!RestoreCookStepArtifacts(receipt, assetsRoot, transaction, Artifacts_,
                                      Generated_, SceneRefs_, sceneReferenced, error))
            return false;
        primary = Artifacts_.back();
        return true;
    }

    [[nodiscard]] bool IsGenerated(std::string_view assetPath) const
    {
        return Generated_.count(std::string(assetPath)) != 0;
    }

    [[nodiscard]] std::vector<CookedArtifact>& Artifacts() { return Artifacts_; }
    [[nodiscard]] const std::vector<CookedArtifact>& Artifacts() const { return Artifacts_; }
    [[nodiscard]] const std::vector<std::string>& SceneRefs() const { return SceneRefs_; }

private:
    std::vector<CookedArtifact> Artifacts_;
    std::unordered_set<std::string> Generated_;
    std::unordered_set<std::string> SeenMaterial_;
    std::vector<std::string> SceneRefs_;
};
