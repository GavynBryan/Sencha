#include "authoring/AnimationLayerChoice.h"

#include "authoring/AnimationNavigation.h"

#include "properties/SettingProperties.h"

#include <anim/AnimRigBinding.h>

#include <optional>
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

PropertyEditResult CommitAnimationLayerChoice(const AnimBoundRig& rig, AnimationNavigation& navigation,
                                              std::string_view value)
{
    const std::optional<std::size_t> layer = ParseChoice(value, LayerNames(rig));
    if (!layer)
        return PropertyEditResult::Refused;
    navigation.Layer = *layer;
    navigation.Rule = -1;
    return PropertyEditResult::Applied;
}
