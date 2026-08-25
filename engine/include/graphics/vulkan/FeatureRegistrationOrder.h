#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

//=============================================================================
// FeatureRegistrationOrder
//
// Resolves the order render features are set up, recorded, and torn down from
// declared dependencies rather than from the order a host happened to call
// AddFeature in.
//
// The edges are real and were previously prose: the shadow feature's Setup
// creates the lighting set layout the forward pass reads when it builds its
// pipeline layout, the sky records before the mesh pass because it fills the
// view with no depth test, and the editor's render feature must tear down
// before the UI feature destroys the ImGui backend its presenter frees
// descriptor sets through. Each of those was a comment above a call, and a
// feature added at the wrong call site was wrong in a way nothing reported.
//
// An edge orders LIFECYCLE always -- a dependency is set up first and torn
// down last -- and orders RECORDING only between features in the same phase,
// since phase order is the renderer's, not a feature's, to decide.
//
// Names no Vulkan and holds no graphics objects: the ordering policy is
// testable without a device, which the renderer that applies it is not.
//=============================================================================

// What a host declares about a feature when staging it. Ids are string
// literals with static storage; the resolver only compares them.
struct FeatureRegistration
{
    std::string_view Id{};
    std::span<const std::string_view> DependsOn{};
};

enum class FeatureOrderFault : std::uint8_t
{
    // Two staged features, or a staged and an already-registered feature,
    // claim the same id.
    DuplicateId,
    // A declared dependency names nothing staged or already registered.
    UnknownDependency,
    // The feature sits in a dependency cycle.
    Cycle,
};

struct FeatureOrderProblem
{
    FeatureOrderFault Fault = FeatureOrderFault::DuplicateId;
    // The feature the fault is reported against.
    std::string_view Id{};
    // For UnknownDependency, the dependency that could not be resolved.
    std::string_view Dependency{};
};

[[nodiscard]] std::string_view ToString(FeatureOrderFault fault);

// Orders `staged` so every feature follows the dependencies it declares.
// `registered` names features already committed in earlier batches, which a
// staged feature may depend on but which do not move.
//
// Ties break by staging index, so a host's own order still decides between
// features that declare nothing about each other -- the resolution is
// deterministic, and a capture compares frame N across builds.
//
// Returns true and fills `order` with indices into `staged` when the graph is
// sound. On any fault it returns false, fills `problems`, and leaves `order`
// empty: an ordering that cannot be trusted registers nothing rather than
// registering most of it. Unlike a per-frame view, a feature is lifecycle --
// a renderer missing its shadow feature should say so once, loudly.
[[nodiscard]] bool ResolveFeatureOrder(
    std::span<const FeatureRegistration> staged,
    std::span<const std::string_view> registered,
    std::vector<std::size_t>& order,
    std::vector<FeatureOrderProblem>& problems);

// The id of a registered feature that declares `id` as a dependency, or empty
// when nothing does. Removal is legal only for a feature nothing depends on,
// and this is what answers that.
[[nodiscard]] std::string_view FindDependent(
    std::span<const FeatureRegistration> registered, std::string_view id);
