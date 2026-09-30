#pragma once

#include "ProjectCatalog.h"

#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <string>
#include <vector>

class UiService;

enum class RackChoice : std::uint8_t
{
    Pick,
    Open,
    Forget,
    New,
    Browse,
};

struct RackRequest
{
    RackChoice Choice = RackChoice::Pick;
    // The project file, for a choice about one.
    std::string Path = {};
};

//=============================================================================
// ProjectRack
//
// The recent-project catalog as the Project workspace's content: a rack of
// cartridges behind the shell, most recent first, a missing file badged. It
// shows what it is given and hands back what the person asked for; the
// workspace decides what that means.
//=============================================================================
class ProjectRack
{
public:
    explicit ProjectRack(UiService& ui) : Ui(ui) {}
    ~ProjectRack();

    ProjectRack(const ProjectRack&) = delete;
    ProjectRack& operator=(const ProjectRack&) = delete;

    // Where it shows from now on: the window's surface for documents a person
    // works in.
    void MoveTo(UiSurfaceId surface);
    // Shown only while its workspace is.
    void SetVisible(bool visible);

    // Per frame, before the UI updates: shows the catalog with `selected`
    // picked, and returns what was asked for since the last frame.
    [[nodiscard]] std::vector<RackRequest> Update(const ProjectCatalog& catalog, const std::string& selected,
                                                  bool creating);

private:
    void Open();
    void Close();

    UiService& Ui;
    UiSurfaceId Surface;
    UiScreenHandle Screen;
    bool Visible = false;
    // What was last shown, so an unchanged catalog does not touch the disk
    // again to ask which files are missing.
    std::vector<ProjectCatalogEntry> Shown;
    std::string ShownSelection;
    bool Published = false;
};
