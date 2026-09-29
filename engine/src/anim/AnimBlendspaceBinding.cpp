#include "AnimRigBinder.h"

#include <anim/AnimBlendspaceData.h>

#include <format>

int AnimRigBinder::BindBlendspaceContent(const std::string& path, const std::string& referrer,
                                         const std::string& field)
{
    for (std::size_t i = 0; i < Out.Contents.size(); ++i)
        if (Out.Contents[i].Path == path && Out.Contents[i].Blendspace >= 0)
            return static_cast<int>(i);

    const AnimBlendspaceData* space = Load<AnimBlendspaceData>(path, kAnimBlendspaceType, referrer, field);
    if (space == nullptr)
        return -1;

    AnimBoundBlendspace bound;
    bound.Path = path;
    bound.AxisCount = static_cast<std::uint8_t>(space->Axes.size());
    bool ok = true;
    for (std::size_t a = 0; a < space->Axes.size() && a < 2; ++a)
    {
        const AnimBlendspaceAxisDecl& decl = space->Axes[a];
        AnimBoundBlendspaceAxis& axis = bound.Axes[a];
        axis.Fact = decl.Fact;
        axis.Min = decl.Min;
        axis.Max = decl.Max;
        axis.FactSlot = Out.FindSlot(decl.Fact);
        const bool numeric = axis.FactSlot >= 0
            && (Out.Slots[static_cast<std::size_t>(axis.FactSlot)].Kind == AnimFactKind::Float
                || Out.Slots[static_cast<std::size_t>(axis.FactSlot)].Kind == AnimFactKind::Int);
        if (!numeric)
        {
            Error("anim.blendspace.axis_fact", path, std::format("$.data.axes[{}].fact", a),
                  std::format("'{}' is not a float or int fact of this rig, so it cannot place the mix.",
                              decl.Fact));
            ok = false;
        }
    }
    for (std::size_t s = 0; s < space->Samples.size(); ++s)
    {
        const AnimBlendspaceSampleDecl& decl = space->Samples[s];
        AnimBoundBlendspaceSample sample;
        sample.Content = FindOrAddClipContent(decl.Clip);
        sample.At[0] = decl.At[0];
        sample.At[1] = decl.At[1];
        if (sample.Content < 0)
        {
            Error("anim.blendspace.clip_unavailable", path, std::format("$.data.samples[{}].clip", s),
                  std::format("'{}' is not a loaded animation clip.", decl.Clip));
            ok = false;
        }
        bound.Samples.push_back(sample);
    }
    if (!ok)
        return -1;

    Out.Blendspaces.push_back(std::move(bound));
    AnimBoundContent content;
    content.Path = path;
    content.Blendspace = static_cast<int>(Out.Blendspaces.size() - 1);
    Out.Contents.push_back(std::move(content));
    return static_cast<int>(Out.Contents.size() - 1);
}
