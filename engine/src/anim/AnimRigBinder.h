#pragma once

#include <anim/AnimRigBinding.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class GameplayTagRegistry;

// Binds one rig into one World. Each part binds in its own file and reports
// through here, so every problem is located the same way.
struct AnimRigBinder
{
    const DataAssetCache& Data;
    const AnimationClipCache* Clips = nullptr;
    const SkeletonCache* Skeletons = nullptr;
    const World& WorldRef;
    AnimBoundRig& Out;
    // Every asset the binding read and the reload version it read.
    std::vector<std::pair<DataAssetHandle, std::uint64_t>>* Versions = nullptr;

    [[nodiscard]] const GameplayTagRegistry* Tags() const;

    void Report(AnimDiagnosticSeverity severity, std::string code, std::string asset,
                std::string field, std::string message);
    void Error(std::string code, std::string asset, std::string field, std::string message)
    {
        Report(AnimDiagnosticSeverity::Error, std::move(code), std::move(asset), std::move(field),
               std::move(message));
    }
    void Warning(std::string code, std::string asset, std::string field, std::string message)
    {
        Report(AnimDiagnosticSeverity::Warning, std::move(code), std::move(asset), std::move(field),
               std::move(message));
    }

    // The tag's id in this World, or an error located at `field`.
    [[nodiscard]] std::optional<GameplayTagId> ResolveTag(const std::string& name, const std::string& asset,
                                                          const std::string& field, std::string_view code);

    template <typename T>
    const T* Load(std::string_view path, std::string_view subtype, std::string_view referrer,
                  std::string_view field)
    {
        const DataAssetHandle handle = Data.Find(path);
        if (!handle.IsValid())
        {
            Error("anim.asset.not_resident", std::string(referrer), std::string(field),
                  std::format("'{}' is not loaded; it is a dependency of this asset and "
                              "should have loaded with it.",
                              path));
            return nullptr;
        }
        if (Versions != nullptr)
            Versions->push_back({ handle, Data.GetReloadVersion(handle) });
        const T* value = Data.TryGet<T>(handle, subtype);
        if (value == nullptr)
            Error("anim.asset.wrong_subtype", std::string(referrer), std::string(field),
                  std::format("'{}' is not a {}.", path, subtype));
        return value;
    }

    void BindFacts(const AnimRigData& rig);
    void BindDerivation(const AnimDerivedFactDecl& decl, const std::string& schemaPath, std::size_t index);
    void BindProviders();
    void BindRequests(const AnimRigData& rig);
    void BindLayers(const AnimRigData& rig);
    // After layers: the rig's skeleton and each layer's bone mask over it.
    void BindMasks(const AnimRigData& rig);
    // After slot maps: every clip the rig plays is keyed to its skeleton.
    void ValidateClipSkeletons();
    void BindBehaviors(const AnimRigData& rig);
    // After behaviors, which overrides name.
    void BindBlendOverrides(const AnimRigData& rig);
    void BindSelectors(const AnimRigData& rig);
    void BindSlotMaps(const AnimRigData& rig);
    // Bound on first use, with its sections' clips as content entries; -1 on failure.
    int BindFlowContent(const std::string& path, const std::string& referrer, const std::string& field);
    // After selectors and slot maps: what a flow requires of the behavior playing it.
    void ValidateFlows();
    // Bound on first use, with its samples' clips as content entries; -1 on failure.
    int BindBlendspaceContent(const std::string& path, const std::string& referrer, const std::string& field);
    // Flows on other layers that one request can drive with `row` share its anchor.
    void ValidateSharedAnchor(const AnimBoundSlotRow& row);
    [[nodiscard]] int FindOrAddClipContent(const std::string& path);
    // After slot maps, which decide the content whose events are bound.
    void BindEvents(const AnimRigData& rig);
};

// How two flows would read one request anchor differently, or empty when they agree.
[[nodiscard]] std::string AnimFlowAnchorDifference(const AnimBoundRig& rig, const AnimBoundFlow& a,
                                                   const AnimBoundFlow& b);

// FNV-1a over text, for the stable keys of flattened rules and merged rows.
[[nodiscard]] std::uint32_t AnimStableKey(std::string_view text);
