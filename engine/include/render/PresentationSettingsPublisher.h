#pragma once

#include <presentation/PresentationOwner.h>
#include <render/PresentationSettings.h>
#include <render/post/PostEffectCatalog.h>

#include <cstdint>
#include <limits>
#include <optional>

class World;

// Below every order a game can write at, so any game contribution wins.
inline constexpr std::int16_t kPresentationSettingsOrder = std::numeric_limits<std::int16_t>::min();

// Holds resolved settings as post-effect contributions for every view of a World.
class PresentationSettingsPublisher
{
public:
    explicit PresentationSettingsPublisher(const PostEffectCatalog& effects);

    // Writes only what changed since the last call.
    void Publish(World& world, const PresentationSettings& settings);

private:
    PostEffectId Bloom;
    PresentationWriterId Writer;
    std::optional<PresentationSettings> Published;
};
