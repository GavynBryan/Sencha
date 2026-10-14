#include "shell/NumberScrub.h"

#include "properties/SettingProperties.h"

#include <utility>

NumberScrub::NumberScrub(std::function<void(bool)> captures)
    : Captures(std::move(captures))
{
}

void NumberScrub::Press(SheetPoint at, std::string_view text, std::vector<std::string> parts,
                        const PropertyDescriptor& bounds)
{
    End(false);
    const std::optional<double> start = ParseNumber(text);
    Pressed = at;
    Motion = ScrubGesture(ScrubScaleFor(bounds));
    Mixed = !start.has_value();
    Start = start.value_or(0.0);
    Parts = std::move(parts);
    Amount = 0.0;
    Phase = ScrubPhase::Pressed;
}

NumberScrub::Took NumberScrub::Take(const InputEvent& event, const Resolve& resolve)
{
    if (Phase == ScrubPhase::Resting)
        return Took::No;
    if (const auto* move = std::get_if<PointerMoveEvent>(&event))
    {
        if (!Motion.Move(move->Delta.X, move->Modifiers.Shift))
            return Took::Yes;
        if (Phase == ScrubPhase::Pressed)
        {
            const Target target = resolve ? resolve(Pressed) : Target{};
            if (target.Source == nullptr || target.Key.empty() || !Session.Begin(*target.Source, target.Key))
            {
                Phase = ScrubPhase::Resting;
                return Took::No;
            }
            WholeVector = target.WholeVector;
            Phase = ScrubPhase::Dragging;
            if (Captures)
                Captures(true);
        }
        Preview(move->Modifiers.Ctrl);
        return Took::Yes;
    }
    if (const auto* up = std::get_if<PointerUpEvent>(&event); up != nullptr && up->Button == MouseButton::Left)
    {
        const bool click = Phase == ScrubPhase::Pressed;
        End(true);
        return click ? Took::Click : Took::Yes;
    }
    if (const auto* key = std::get_if<KeyDownEvent>(&event))
    {
        if (key->Key == SDLK_ESCAPE)
            End(false);
        return Took::Yes;
    }
    if (std::holds_alternative<KeyUpEvent>(event))
        return Took::Yes;
    if (std::holds_alternative<FocusLostEvent>(event))
        End(false);
    return Took::No;
}

std::string NumberScrub::Written() const
{
    const std::string moved = NumberText(Start + Amount);
    if (!WholeVector)
        return moved;
    std::vector<std::string> parts = Parts;
    if (Pressed.Element >= 0 && static_cast<std::size_t>(Pressed.Element) < parts.size())
        parts[static_cast<std::size_t>(Pressed.Element)] = moved;
    return JoinVector(parts);
}

void NumberScrub::Preview(bool coarse)
{
    Amount = Motion.Amount(Mixed ? 0.0 : Start, coarse);
    if (Mixed)
        (void)Session.PreviewBy(Amount);
    else
        (void)Session.Preview(Written());
}

void NumberScrub::End(bool commit)
{
    if (Phase == ScrubPhase::Dragging)
    {
        if (!commit)
            Session.Cancel();
        else if (Mixed)
            (void)Session.CommitBy(Amount);
        else
            (void)Session.Commit(Written());
        if (Captures)
            Captures(false);
    }
    Phase = ScrubPhase::Resting;
}
