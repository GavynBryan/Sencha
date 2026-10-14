#pragma once

#include "authoring/AbilityStrip.h"
#include "ui/UiScreenSlot.h"

#include <math/geometry/2d/Rect2d.h>
#include <ui/UiSurface.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class UiService;

enum class AbilityStripAsk : std::uint8_t
{
    Pick,
    // A drag began on the element named, and where it ends.
    Carry,
    Drop,
    Put,
    // An op wanted in the slot named.
    Add,
    AddPhase,
};

struct AbilityStripRequest
{
    AbilityStripAsk Ask = AbilityStripAsk::Pick;
    std::string Key = {};
};

//=============================================================================
// AbilityPhaseStrip
// The open ability's phases as cards over the workspace's content. It shows the
// rows it is given and hands back, in order, what the person asked for.
//=============================================================================
class AbilityPhaseStrip
{
public:
    explicit AbilityPhaseStrip(UiService& ui);
    ~AbilityPhaseStrip();

    AbilityPhaseStrip(const AbilityPhaseStrip&) = delete;
    AbilityPhaseStrip& operator=(const AbilityPhaseStrip&) = delete;

    void MoveTo(UiSurfaceId surface);
    void SetVisible(bool visible);

    // Per frame, before the UI updates: placed over `region` in window pixels.
    [[nodiscard]] std::vector<AbilityStripRequest> Update(const Rect2d& region, const AbilityStripRows& rows,
                                                          std::string_view empty);
    [[nodiscard]] UiScreenHandle CurrentScreen() const { return Screen.Handle(); }

private:
    UiService& Ui;
    UiScreenSlot Screen;
    bool Visible = false;
    AbilityStripRows Shown;
    bool Published = false;
};
