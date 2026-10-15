#pragma once

#include <core/identity/StrongId.h>
#include <math/Vec.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct DebugLine
{
    Vec3d From;
    Vec3d To;
    Vec4 Color;
};

// Lines one producer wants shown: those the scene may hide, and those drawn
// over it.
class DebugLineList
{
public:
    void Clear();
    void AddTested(const Vec3d& from, const Vec3d& to, const Vec4& color);
    void AddOnTop(const Vec3d& from, const Vec3d& to, const Vec4& color);

    [[nodiscard]] std::span<const DebugLine> Tested() const { return TestedLines; }
    [[nodiscard]] std::span<const DebugLine> OnTop() const { return OnTopLines; }
    [[nodiscard]] bool Empty() const { return TestedLines.empty() && OnTopLines.empty(); }

private:
    std::vector<DebugLine> TestedLines;
    std::vector<DebugLine> OnTopLines;
};

using DebugLineChannelId = StrongId<struct DebugLineChannelIdTag, std::uint32_t>;

// Simulation-side debug drawing, as a World resource. A producer owns a named
// channel and replaces its lines when it has new state; render extraction
// reads every channel each frame.
class DebugLineChannels
{
public:
    // The channel named `name`, created empty the first time it is asked for.
    [[nodiscard]] DebugLineChannelId Channel(std::string_view name);
    [[nodiscard]] DebugLineList& Lines(DebugLineChannelId channel);
    [[nodiscard]] std::span<const DebugLineList> All() const { return Lists; }

private:
    std::vector<std::string> Names;
    std::vector<DebugLineList> Lists;
};

// What an eye at `apex` looking along `axis` covers out to `length`: spokes to
// the rim and arcs over the range's sphere. A half-angle of pi draws a sphere.
void AppendCone(DebugLineList& lines, const Vec3d& apex, const Vec3d& axis, float halfAngle,
                float length, const Vec4& color);

// Three axis-aligned strokes through `point`, drawn over the scene.
void AppendMarker(DebugLineList& lines, const Vec3d& point, float size, const Vec4& color);
