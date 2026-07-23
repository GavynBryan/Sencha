#pragma once

#include <assets/cook/LightingCookParams.h>
#include <assets/cook/CookControl.h>
#include <core/assets/AssetRef.h>
#include <core/json/JsonValue.h>
#include <math/Vec.h>
#include <project/CookProfile.h>

#include <filesystem>
#include <optional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Editor-side level cook. Collection snapshots editor and asset state on the
// owner thread. Execution consumes only that immutable snapshot and publishes
// staged artifacts, so the same kernel supports synchronous tools and the
// editor's background CookSession.

struct DocumentCookResult
{
    bool                     Success = false;
    bool                     CacheHit = false;
    bool                     Cancelled = false;
    std::string              ProfileId;
    std::string              Error;
    std::vector<std::string> ReusedSteps;
    std::vector<std::string> GeneratedMeshPaths; // asset:// per cell, in cook order
    std::filesystem::path    CookedScenePath;
    std::filesystem::path    ManifestPath;
    std::filesystem::path    CollisionSidecarPath;
    uint64_t                 ContentHash = 0; // the hash the cooked cache keyed this cook by
    std::size_t              CellCount = 0;
    std::size_t              DirectLightCount = 0;   // lights baked this cook
    std::size_t              ProbeVolumeCount = 0;   // authored volumes baked
    std::size_t              ProbeCount = 0;         // total probes across them
    std::uint32_t            LightmapAtlasWidth = 0; // 0 when nothing baked
    std::uint32_t            LightmapAtlasHeight = 0;
    // The luxel size the atlas actually packed at; differs from the requested
    // size only when MaxAtlasSize forced a density clamp (worth a log line).
    float                    EffectiveLuxelSize = 0.0f;
};

struct DocumentCookOptions
{
    // Null selects the protected Full profile.
    const CookProfile* Profile = nullptr;
    bool ForceRebuild = false;
    std::shared_ptr<CookControl> Control;
    // Optional prefix for content-addressed publication. World cooks use this
    // so the manifest switches generations only after every zone succeeds.
    std::string OutputNamespace;
};

class EditorDocument;
class LoggingProvider;
struct RuntimeAssets;
struct DocumentCookRequest;
struct DocumentCookSnapshot;

// Immutable, move-only snapshot consumed by the long-running cook backend.
// Collection resolves editor registry state and asset handles on the caller's
// thread; execution never touches the live document or AssetSystem.
class DocumentCookInput
{
public:
    struct Data;

    DocumentCookInput(DocumentCookInput&&) noexcept;
    DocumentCookInput& operator=(DocumentCookInput&&) noexcept;
    ~DocumentCookInput();

    DocumentCookInput(const DocumentCookInput&) = delete;
    DocumentCookInput& operator=(const DocumentCookInput&) = delete;

    // The resolved policy and captured document state. Exposed so collection is
    // testable without executing a cook.
    [[nodiscard]] const DocumentCookRequest& Request() const;
    [[nodiscard]] const DocumentCookSnapshot& Snapshot() const;

private:
    explicit DocumentCookInput(std::unique_ptr<Data> data);
    std::unique_ptr<Data> Input;

    friend std::optional<DocumentCookInput> CollectDocumentCookInput(
        const EditorDocument&, const std::filesystem::path&, double,
        LoggingProvider&, RuntimeAssets*, const LightingCookParams&,
        std::span<const ProbeHaloZone>, const DocumentCookOptions&, std::string*);
    friend DocumentCookResult ExecuteDocumentCook(
        DocumentCookInput, std::string_view, std::string_view,
        const std::filesystem::path&, LoggingProvider&);
};

[[nodiscard]] std::optional<DocumentCookInput> CollectDocumentCookInput(
    const EditorDocument& document,
    const std::filesystem::path& assetsRoot,
    double cellSize,
    LoggingProvider& logging,
    RuntimeAssets* assets = nullptr,
    const LightingCookParams& lightmapParams = {},
    std::span<const ProbeHaloZone> halo = {},
    const DocumentCookOptions& options = {},
    std::string* error = nullptr);

[[nodiscard]] DocumentCookResult ExecuteDocumentCook(
    DocumentCookInput input,
    std::string_view levelName,
    std::string_view sourceRel,
    const std::filesystem::path& assetsRoot,
    LoggingProvider& logging);

// Cooks the authored level at `authoredLevelPath` into `assetsRoot`. cellSize is
// the spatial grid size (a cvar at the call site). Requires
// RegisterDocumentSerializers() to have run (the cooked-scene assembly uses the
// scene serializers for passthrough game components). Brush-only: passthrough
// entities carrying asset-handle components (e.g. StaticMesh props) need the
// live overload, which supplies the asset system to resolve their refs.
// `logging` and `assets` are optional: null logging cooks silently (headless),
// null assets cooks brush-only (passthrough asset-handle components need the
// shared asset system to resolve their refs).
// `halo` is neighbor-zone occluder geometry (CollectZoneBakeHalo records):
// zones within probe-ray reach join the bake's occlusion set and the cook
// hash. A world cook passes the sibling zones; a standalone level cook has
// none.
[[nodiscard]] DocumentCookResult CookDocument(const std::filesystem::path& authoredLevelPath,
                                        const std::filesystem::path& assetsRoot,
                                        double cellSize,
                                        LoggingProvider* logging = nullptr,
                                        RuntimeAssets* assets = nullptr,
                                        const LightingCookParams& lightmapParams = {},
                                        std::span<const ProbeHaloZone> halo = {},
                                        const DocumentCookOptions& options = {});

// Collects the saved level's bake-occluding world geometry (brush faces plus
// placements that cast into the bake) as one halo record for NEIGHBOR zone
// cooks: triangles, their bounds, and a content hash over them. Null return
// means the level failed to load. `assets` as for CookDocument (placements
// need it to resolve their mesh refs).
[[nodiscard]] std::optional<ProbeHaloZone> CollectZoneBakeHalo(
    const std::filesystem::path& authoredLevelPath,
    const std::filesystem::path& assetsRoot,
    LoggingProvider* logging = nullptr,
    RuntimeAssets* assets = nullptr);

// Cooks the live (possibly unsaved) editor document, named `levelName` (the
// artifact stem). The document is snapshotted internally, so the caller's
// document is not modified. This is the editor Cook button's path: author, cook,
// play without a save-to-disk round trip. `assets` is the shared engine asset
// system (the same one the editor authored through and the runtime loads with),
// so passthrough asset-handle components serialize handle->path consistently;
// null cooks brush-only (no prop support), the headless path tests use without
// graphics. `logging` is required (a sink-less provider is fine for headless).
[[nodiscard]] DocumentCookResult CookDocument(const EditorDocument& liveDocument,
                                        std::string_view levelName,
                                        const std::filesystem::path& assetsRoot,
                                        double cellSize,
                                        LoggingProvider& logging,
                                        RuntimeAssets* assets = nullptr,
                                        const LightingCookParams& lightmapParams = {},
                                        const DocumentCookOptions& options = {});
