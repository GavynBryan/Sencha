#pragma once

#include <memory>
#include <string_view>
#include <utility>

class Engine;
class IRenderFeature;

// Adds a workspace's render feature to the main renderer and sets it up; null
// when it did not set up, and then it never draws.
[[nodiscard]] IRenderFeature* AttachRenderFeature(Engine& engine, std::unique_ptr<IRenderFeature> feature,
                                                  std::string_view id);

template <typename Feature>
[[nodiscard]] Feature* AttachRenderFeature(Engine& engine, std::unique_ptr<Feature> feature, std::string_view id)
{
    return static_cast<Feature*>(AttachRenderFeature(engine, std::unique_ptr<IRenderFeature>(std::move(feature)), id));
}

// Removes a render feature before the state it borrows is destroyed. A refusal
// would leave it drawing through dangling references, so it is fatal.
void DetachRenderFeature(Engine& engine, IRenderFeature*& feature);

template <typename Feature>
void DetachRenderFeature(Engine& engine, Feature*& feature)
{
    IRenderFeature* base = feature;
    DetachRenderFeature(engine, base);
    feature = nullptr;
}
