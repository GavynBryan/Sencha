#include "input/InputControlCapture.h"

#include <input/InputControl.h>
#include <input/SdlInputCapture.h>

#include <SDL3/SDL.h>

#include <utility>

void InputControlCapture::Arm(std::string fieldPath,
                              InputSlotAcceptance acceptance,
                              std::string documentPath,
                              std::uint64_t documentRevision)
{
    Armed = true;
    Acceptance = acceptance;
    ArmedField = std::move(fieldPath);
    ArmedDocument = std::move(documentPath);
    ArmedRevision = documentRevision;

    // An earlier capture nobody collected is abandoned: it was meant for a slot
    // the author has moved on from.
    CapturedName.clear();
    CapturedField.clear();
    CapturedDocument.clear();
    CapturedRevision = 0;
}

void InputControlCapture::Disarm()
{
    Armed = false;
    ArmedField.clear();
    ArmedDocument.clear();
    ArmedRevision = 0;
}

bool InputControlCapture::IsArmedAt(std::string_view fieldPath) const
{
    return Armed && ArmedField == fieldPath;
}

std::optional<std::string> InputControlCapture::TakeCapture(std::string_view fieldPath,
                                                            std::string_view documentPath,
                                                            std::uint64_t documentRevision)
{
    if (CapturedField.empty() || CapturedField != fieldPath)
        return std::nullopt;

    const bool sameDocument =
        CapturedDocument == documentPath && CapturedRevision == documentRevision;
    CapturedField.clear();
    CapturedDocument.clear();
    CapturedRevision = 0;
    std::string name = std::move(CapturedName);
    CapturedName.clear();

    // The document moved between arming and the press, so the path this was
    // armed for may name a different binding now. Dropping it costs one retry;
    // delivering it would silently rebind the wrong control.
    if (!sameDocument)
        return std::nullopt;
    return name;
}

bool InputControlCapture::HandleSdlEvent(const SDL_Event& event)
{
    if (!Armed)
        return false;

    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        Disarm();
        return true;
    }

    const std::optional<InputControl> control =
        InputControlFromSdlEvent(event, kAxisThreshold);
    if (!control.has_value())
        return false;

    // A control this slot cannot take leaves the listen armed: the author is
    // reaching for one that fits, and a pad resting against a thumb should not
    // end the wait. Listening and picking answer to the same rule, so Listen
    // cannot bind what Pick would not offer.
    if (!Acceptance.Accepts(control->Source))
        return false;

    CapturedName = FormatInputControl(*control);
    CapturedField = ArmedField;
    CapturedDocument = ArmedDocument;
    CapturedRevision = ArmedRevision;
    Disarm();
    return true;
}
