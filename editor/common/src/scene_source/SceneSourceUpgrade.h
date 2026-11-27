#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct SceneSourceUpgrade
{
    std::string Text;
    std::vector<std::string> Changed;
    std::vector<std::string> Refused;
};

// Refused keeps Text as given, no Changed means already current, nullopt means not a scene source.
[[nodiscard]] std::optional<SceneSourceUpgrade> UpgradeSceneSourceText(std::string_view text,
                                                                     std::string* error = nullptr);
