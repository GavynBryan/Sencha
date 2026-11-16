#include "GradientRampPool.h"

#include <utility>

UiGeneratedTextureId GradientRampPool::Acquire(std::vector<std::byte> pixels,
                                               UiGeneratedTextureId& nextGeneratedId,
                                               UiTextureChanges& changes)
{
    const std::string_view key(reinterpret_cast<const char*>(pixels.data()), pixels.size());
    if (const auto shared = RampByPixels.find(key); shared != RampByPixels.end())
    {
        ++Ramps.at(shared->second).Users;
        return shared->second;
    }

    const UiGeneratedTextureId id = nextGeneratedId++;
    const Ramp& ramp = Ramps.emplace(id, Ramp{ std::string(key), 1 }).first->second;
    RampByPixels.emplace(ramp.Pixels, id);
    changes.Uploads.push_back(UiTextureUpload{ id, RenderExtent{ kUiGradientRampTexels, 1 }, std::move(pixels) });
    return id;
}

void GradientRampPool::Release(UiGeneratedTextureId ramp)
{
    const auto found = Ramps.find(ramp);
    if (found != Ramps.end() && found->second.Users > 0 && --found->second.Users == 0)
        Unused.push_back(ramp);
}

void GradientRampPool::ReleaseUnused(UiTextureChanges& changes)
{
    for (const UiGeneratedTextureId ramp : Unused)
    {
        const auto found = Ramps.find(ramp);
        if (found == Ramps.end() || found->second.Users > 0)
            continue;
        RampByPixels.erase(found->second.Pixels);
        Ramps.erase(found);
        changes.Released.push_back(ramp);
    }
    Unused.clear();
}
