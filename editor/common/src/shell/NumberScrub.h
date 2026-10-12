#pragma once

#include "input/InputEvent.h"
#include "properties/PropertyEditSession.h"
#include "shell/ScrubGesture.h"
#include "shell/SheetPoint.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// A number pressed in a sheet: dragged across, it moves, previewed live as one
// edit session and committed when let go; let go where it was pressed, it was
// a click. While pressed or dragged it has the pointer and the keys: Escape
// cancels, and no other key reaches anything until it is let go.
class NumberScrub
{
public:
    // Where its edits go, asked for when the press becomes a drag. `WholeVector`
    // when the number has no key of its own and its vector is written whole.
    struct Target
    {
        IPropertySource* Source = nullptr;
        std::string Key;
        bool WholeVector = false;
    };
    using Resolve = std::function<Target(const SheetPoint&)>;

    // `captures` is told when a drag takes the pointer and when it lets go.
    explicit NumberScrub(std::function<void(bool)> captures);

    // A press on a number showing `text`, of a vector whose numbers are `parts`.
    void Press(SheetPoint at, std::string_view text, std::vector<std::string> parts, const PropertyDescriptor& bounds);

    enum class Took : std::uint8_t
    {
        No,
        Yes,
        // Let go where it was pressed: the number is typed into instead.
        Click,
    };
    Took Take(const InputEvent& event, const Resolve& resolve);
    // Ended from outside -- what it edits moved on, or went away.
    void End(bool commit);

    [[nodiscard]] bool Holds() const { return Phase != ScrubPhase::Resting; }
    [[nodiscard]] const SheetPoint& At() const { return Pressed; }

private:
    enum class ScrubPhase : std::uint8_t { Resting, Pressed, Dragging };

    // What the drag writes now: the number, or its vector with the number in it.
    [[nodiscard]] std::string Written() const;
    void Preview(bool coarse);

    std::function<void(bool)> Captures;
    ScrubPhase Phase = ScrubPhase::Resting;
    SheetPoint Pressed;
    ScrubGesture Motion{ ScrubScale{} };
    PropertyEditSession Session;
    bool WholeVector = false;
    // Where the number began, or that it stands for several and moves each by
    // the amount.
    double Start = 0.0;
    bool Mixed = false;
    std::vector<std::string> Parts;
    double Amount = 0.0;
};
