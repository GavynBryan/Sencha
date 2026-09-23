#include "authoring/AnimationRigEdits.h"

namespace
{
    JsonValue* Layer(JsonValue& root, std::size_t layer)
    {
        JsonValue::Array* layers = AnimRigLayers(root);
        return layers != nullptr && layer < layers->size() && (*layers)[layer].IsObject() ? &(*layers)[layer] : nullptr;
    }

    JsonValue::Array* Mask(JsonValue& root, std::size_t layer, bool create)
    {
        JsonValue* entry = Layer(root, layer);
        if (entry == nullptr)
            return nullptr;
        JsonValue* mask = entry->Find("mask");
        if (mask == nullptr && create)
        {
            entry->AsObject().emplace_back("mask", JsonValue(JsonValue::Array{}));
            mask = &entry->AsObject().back().second;
        }
        return mask != nullptr && mask->IsArray() ? &mask->AsArray() : nullptr;
    }
}

JsonValue::Array* AnimRigLayers(JsonValue& root)
{
    JsonValue* data = root.Find("data");
    JsonValue* layers = data != nullptr ? data->Find("layers") : nullptr;
    return layers != nullptr && layers->IsArray() ? &layers->AsArray() : nullptr;
}

bool AddAnimMaskStep(JsonValue& root, std::size_t layer, std::string joint, bool exclude, bool subtree)
{
    JsonValue::Array* mask = Mask(root, layer, true);
    if (mask == nullptr || joint.empty())
        return false;
    JsonValue::Object step{ { "joint", JsonValue(std::move(joint)) } };
    if (exclude)
        step.emplace_back("exclude", JsonValue(true));
    if (!subtree)
        step.emplace_back("subtree", JsonValue(false));
    mask->emplace_back(std::move(step));
    return true;
}

bool RemoveAnimMaskStep(JsonValue& root, std::size_t layer, std::size_t step)
{
    JsonValue::Array* mask = Mask(root, layer, false);
    if (mask == nullptr || step >= mask->size())
        return false;
    mask->erase(mask->begin() + static_cast<std::ptrdiff_t>(step));
    // No steps left is no mask.
    if (mask->empty())
        (void)ClearAnimMask(root, layer);
    return true;
}

bool ClearAnimMask(JsonValue& root, std::size_t layer)
{
    JsonValue* entry = Layer(root, layer);
    if (entry == nullptr || entry->Find("mask") == nullptr)
        return false;
    std::erase_if(entry->AsObject(), [](const auto& member) { return member.first == "mask"; });
    return true;
}

std::vector<std::uint8_t> AnimMaskCoverage(const AnimBoundRig& rig)
{
    std::vector<std::uint8_t> coverage(rig.JointCount, 0);
    for (std::size_t l = 0; l < rig.Layers.size() && l < kAnimMaxLayers; ++l)
        for (std::size_t j = 0; j < coverage.size(); ++j)
            if (rig.Layers[l].Covers(j))
                coverage[j] |= static_cast<std::uint8_t>(1u << l);
    return coverage;
}
