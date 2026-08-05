#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

union SDL_Event;

// Fills a control slot from the control the author presses, rather than from
// the name they remember.
//
// One slot at a time: arming replaces any previous arm, so the surface can
// never be listening in two places. A press is claimed outright -- the caller
// stops the event reaching the UI -- because binding Escape or a mouse button
// must not also dismiss a popup or click whatever is underneath.
//
// The captured name is delivered on a later frame, when the form next draws the
// slot that asked for it. The armed document and revision are recorded so a
// capture cannot land on the wrong binding if the document changed in between:
// array indices move, and the path armed a moment ago may now name something
// else.
class InputControlCapture
{
public:
    void Arm(std::string fieldPath,
             bool buttonsOnly,
             std::string documentPath,
             std::uint64_t documentRevision);
    void Disarm();

    [[nodiscard]] bool IsArmed() const { return Armed; }
    [[nodiscard]] bool IsArmedAt(std::string_view fieldPath) const;

    // The captured control name, once, for the slot that armed it. Reports
    // nothing when the document moved under the arm.
    [[nodiscard]] std::optional<std::string> TakeCapture(std::string_view fieldPath,
                                                         std::string_view documentPath,
                                                         std::uint64_t documentRevision);

    // Returns true when the event was consumed. Only ever consumes while armed:
    // escape cancels, a control binds, and anything that names no control is
    // left alone so hunting for a button does not end the listen.
    bool HandleSdlEvent(const SDL_Event& event);

    // How far a stick or trigger must move before it counts as a press.
    static constexpr float kAxisThreshold = 0.5f;

private:
    bool Armed = false;
    bool ButtonsOnly = false;
    std::string ArmedField;
    std::string ArmedDocument;
    std::uint64_t ArmedRevision = 0;

    // The arm's identity travels with the capture: disarming on the press
    // clears the armed fields, and delivery still has to know which document
    // and revision the arm belonged to.
    std::string CapturedName;
    std::string CapturedField;
    std::string CapturedDocument;
    std::uint64_t CapturedRevision = 0;
};
