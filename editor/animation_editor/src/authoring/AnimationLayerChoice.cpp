#include "authoring/AnimationLayerChoice.h"

#include "properties/SettingProperties.h"

#include <anim/AnimRigBinding.h>

#include <vector>

namespace
{
std::vector<std::string_view> LayerNames(const AnimBoundRig& rig)
{
    std::vector<std::string_view> names;
    for (const AnimBoundLayer& layer : rig.Layers)
        names.push_back(layer.NameText);
    return names;
}
} // namespace

PropertyNode AnimationLayerChoice(const AnimBoundRig& rig, std::size_t layer)
{
    return ChoiceProperty("layer", "Layer", LayerNames(rig), layer);
}

std::optional<std::size_t> ParseAnimationLayerChoice(const AnimBoundRig& rig, std::string_view value)
{
    return ParseChoice(value, LayerNames(rig));
}
