#pragma once

#include <math/geometry/2d/Rect2d.h>

#include <cstdint>
#include <optional>

// Whether a content rect is large enough to show a stage's picture in.
[[nodiscard]] bool CanShowStage(const Rect2d& content);

// A press on a stage turned into turntable orbit: once it moves past a few
// pixels it orbits, a drag right orbiting right and a drag up orbiting above;
// let go before then, it was a click where it was pressed.
class StageOrbitDrag
{
public:
    struct Turn
    {
        float Yaw = 0.0f;
        float Pitch = 0.0f;
    };

    void Press(Vec2d at);
    // The turn a move by `moved` to `at` makes, once the press has become an orbit.
    [[nodiscard]] std::optional<Turn> Move(Vec2d at, Vec2d moved);
    // True when the press lets go without having orbited.
    [[nodiscard]] bool Release();
    void Cancel() { State = Phase::Idle; }
    [[nodiscard]] bool IsHeld() const { return State != Phase::Idle; }

private:
    enum class Phase : std::uint8_t
    {
        Idle,
        Pressed,
        Orbiting,
    };

    Phase State = Phase::Idle;
    Vec2d PressedAt{};
};
