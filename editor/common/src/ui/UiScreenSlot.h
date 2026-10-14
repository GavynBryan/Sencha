#pragma once

#include <ui/UiScreenDesc.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <functional>

class UiService;

// The one screen a presenter keeps on a surface it may be moved to: opened
// when wanted, closed when not or when this goes, and reopened on the new
// surface when moved while open.
class UiScreenSlot
{
public:
    UiScreenSlot(UiService& ui, std::function<UiScreenDesc()> describe, UiSurfaceId surface = {});
    ~UiScreenSlot();

    UiScreenSlot(const UiScreenSlot&) = delete;
    UiScreenSlot& operator=(const UiScreenSlot&) = delete;

    void MoveTo(UiSurfaceId surface);
    // Whether it is open now: it cannot open before it has a surface.
    bool Open();
    void Close();

    [[nodiscard]] bool IsOpen() const { return Screen.IsValid(); }
    [[nodiscard]] UiScreenHandle Handle() const { return Screen; }
    [[nodiscard]] UiSurfaceId Surface() const { return Where; }

private:
    UiService& Ui;
    std::function<UiScreenDesc()> Describe;
    UiSurfaceId Where;
    UiScreenHandle Screen;
};
