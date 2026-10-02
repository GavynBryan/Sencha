#include "authoring/AnimationRootMotionProperties.h"

#include "authoring/AnimationPreviewWorkspace.h"

#include "properties/SettingProperties.h"

#include <anim/AnimRootMotion.h>
#include <anim/AnimationClipCache.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>

namespace
{
constexpr std::string_view kWalls = "walls.";
constexpr std::size_t kBlockedShown = 12;
constexpr float kDegrees = 57.29578f;

// The wall a key names, and which of its sizes: "walls.2.centre".
struct WallKey
{
    std::size_t Wall = 0;
    std::string_view Part;
};

std::optional<WallKey> WallOf(std::string_view key)
{
    if (!key.starts_with(kWalls))
        return std::nullopt;
    key.remove_prefix(kWalls.size());
    WallKey wall;
    const auto parsed = std::from_chars(key.data(), key.data() + key.size(), wall.Wall);
    if (parsed.ec != std::errc{})
        return std::nullopt;
    wall.Part = key.substr(static_cast<std::size_t>(parsed.ptr - key.data()));
    if (wall.Part.starts_with('.'))
        wall.Part.remove_prefix(1);
    return wall;
}

PropertyNode Vector3(std::string key, std::string label, const Vec3d& value)
{
    const std::array<double, 3> parts{ value.X, value.Y, value.Z };
    return VectorProperty(std::move(key), std::move(label), parts);
}
} // namespace

std::vector<PropertyNode> AnimationRootMotionProperties::Nodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig* rig = session.Rig();
    if (!session.IsOpen() || rig == nullptr)
        return { NoteProperty("none", "Root motion", "Open a rig to see what its clips carry.") };
    const std::optional<AnimationScenarioMovement>& movement = session.Scenario().Movement;
    std::vector<PropertyNode> nodes{ ToggleProperty(
        "moves", "Move the character", movement.has_value(),
        "Stand it on a floor and move it through the game's movement pipeline, carried by root motion and "
        "stopped by walls. Off, it poses in place.") };
    if (movement)
    {
        PropertyNode walls = GroupProperty("walls", "Walls");
        walls.Descriptor.Verbs = { { .Id = "add", .Label = "Add a Wall Ahead" } };
        for (std::size_t w = 0; w < movement->Walls.size(); ++w)
        {
            PropertyNode wall = GroupProperty(std::format("{}{}", kWalls, w), std::format("Wall {}", w + 1));
            wall.Descriptor.Verbs = { { .Id = "remove", .Label = "Remove" } };
            wall.Children = { Vector3(std::format("{}{}.centre", kWalls, w), "Centre", movement->Walls[w].Center),
                              Vector3(std::format("{}{}.half", kWalls, w), "Half size",
                                      movement->Walls[w].HalfExtents) };
            walls.Children.push_back(std::move(wall));
        }
        nodes.push_back(std::move(walls));
    }

    const AnimationPreviewTickRecord* record = Workspace.Rig.Inspected();
    PropertyNode clip = GroupProperty("clip", "Base layer's clip");
    const AnimationPreviewLayerRecord* base = record != nullptr && !record->Layers.empty() ? &record->Layers.front()
                                                                                           : nullptr;
    const AnimationClipData* data = base != nullptr && base->Clip < rig->Contents.size()
                                        ? Workspace.Clips().Get(rig->Contents[base->Clip].Clip)
                                        : nullptr;
    if (data == nullptr)
        clip.Children.push_back(NoteProperty("clip.none", "Clip", "It plays no clip."));
    else
    {
        const std::string& path = rig->Contents[base->Clip].Path;
        clip.Children.push_back(NoteProperty("clip.path", "Clip", path));
        if (!data->Root)
            clip.Children.push_back(NoteProperty(
                "clip.root", "Root motion",
                "None. Ask for it with \"root_motion\": true on the clip in its source's import sidecar."));
        else
        {
            const AnimBoundBehavior* behavior = rig->FindBehavior(base->Behavior);
            clip.Children.push_back(NoteProperty("clip.root", "Root motion",
                                                 behavior != nullptr && behavior->Policy.RootMotion
                                                     ? "Carrying the character"
                                                     : "Its behavior is not root motion, so it plays in place"));
            const AnimRootPose now = SampleAnimRootCurve(*data->Root, base->TimeSeconds);
            clip.Children.push_back(NoteProperty(
                "clip.now", "Now",
                std::format("right {:.2f} m, back {:.2f} m, turn {:.0f} deg", now.X, now.Z, now.Yaw * kDegrees)));
        }
    }
    nodes.push_back(std::move(clip));

    if (movement)
    {
        float carried = 0.0f;
        float achieved = 0.0f;
        std::vector<AnimTick> blocked;
        for (const AnimationPreviewTickRecord& tick : session.History())
        {
            if (record != nullptr && tick.Tick > record->Tick)
                break;
            if (!tick.Movement || !tick.Movement->Carried)
                continue;
            carried += tick.Movement->Requested.Magnitude();
            achieved += tick.Movement->Achieved.Magnitude();
            if (tick.Movement->Blocked)
                blocked.push_back(tick.Tick);
        }
        PropertyNode travel = GroupProperty("travel", "Travel");
        travel.Children.push_back(
            NoteProperty("travel.sum", "So far", std::format("carried {:.2f} m, achieved {:.2f} m", carried, achieved)));
        std::string stops = blocked.empty() ? std::string("Nothing stopped it.")
                                            : std::format("Stopped short on {} ticks:", blocked.size());
        for (std::size_t i = 0; i < blocked.size() && i < kBlockedShown; ++i)
            stops += std::format("{}{}", i == 0 ? " " : ", ", blocked[i]);
        if (blocked.size() > kBlockedShown)
            stops += ", ...";
        travel.Children.push_back(NoteProperty("travel.blocked", "Walls", stops));
        nodes.push_back(std::move(travel));
    }
    return nodes;
}

bool AnimationRootMotionProperties::BeginEdit(std::string_view key)
{
    return key == "moves" || WallOf(key).has_value();
}

PropertyEditResult AnimationRootMotionProperties::CommitEdit(std::string_view key, std::string_view value)
{
    AnimationPreviewSession& session = Workspace.Rig.Simulation;
    if (key == "moves")
    {
        const std::optional<bool> on = ParseToggle(value);
        if (!on)
            return PropertyEditResult::Refused;
        session.SetMovement(*on ? std::optional(session.Scenario().Movement.value_or(AnimationScenarioMovement{}))
                                : std::nullopt);
        return PropertyEditResult::Applied;
    }
    std::optional<AnimationScenarioMovement> movement = session.Scenario().Movement;
    const std::optional<WallKey> wall = WallOf(key);
    const std::optional<std::vector<double>> parts = ParseVector(value, 3);
    if (!movement || !wall || wall->Wall >= movement->Walls.size() || !parts)
        return PropertyEditResult::Refused;
    const Vec3d vector(static_cast<float>((*parts)[0]), static_cast<float>((*parts)[1]), static_cast<float>((*parts)[2]));
    AnimationScenarioWall& edited = movement->Walls[wall->Wall];
    if (wall->Part == "centre")
        edited.Center = vector;
    else if (wall->Part == "half")
        edited.HalfExtents = Vec3d(std::max(vector.X, 0.01f), std::max(vector.Y, 0.01f), std::max(vector.Z, 0.01f));
    else
        return PropertyEditResult::Gone;
    session.SetMovement(std::move(movement));
    return PropertyEditResult::Applied;
}

PropertyEditResult AnimationRootMotionProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    AnimationPreviewSession& session = Workspace.Rig.Simulation;
    std::optional<AnimationScenarioMovement> movement = session.Scenario().Movement;
    if (!movement)
        return PropertyEditResult::Gone;
    if (key == "walls" && verb == "add")
        // Two metres ahead of the start position, facing the character.
        movement->Walls.push_back({ .Center = Vec3d(0.0f, 1.0f, -2.0f), .HalfExtents = Vec3d(2.0f, 1.0f, 0.25f) });
    else if (const std::optional<WallKey> wall = WallOf(key);
             wall && wall->Part.empty() && verb == "remove" && wall->Wall < movement->Walls.size())
        movement->Walls.erase(movement->Walls.begin() + static_cast<std::ptrdiff_t>(wall->Wall));
    else
        return PropertyEditResult::Gone;
    session.SetMovement(std::move(movement));
    return PropertyEditResult::Applied;
}
