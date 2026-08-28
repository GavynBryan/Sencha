#pragma once

#include "ui/IEditorPanel.h"

#include <filesystem>
#include <string>
#include <vector>

class CommandStack;
class SelectionService;
class WorldDocument;

// Lists the .sscene sources under the project's content roots for placement:
// search to filter, drag a row into a viewport to place it where it lands
// (surface snap, working-grid fallback), or place it at the origin from the
// row itself. One undoable command either way, leaving the placement's root
// selected.
class SceneBrowserPanel : public IEditorPanel
{
public:
    SceneBrowserPanel(WorldDocument& world, SelectionService& selection,
                      CommandStack& commands,
                      std::vector<std::filesystem::path> contentRoots);

    std::string_view GetTitle() const override { return "Scenes"; }
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Left; }

    // The drag payload type viewport drop targets accept; the payload bytes
    // are the asset:// source path.
    static constexpr const char* kDragPayloadType = "KYUSU_SCENE_SOURCE";

private:
    struct Entry
    {
        std::string AssetPath; // asset://...
        std::string Label;     // file stem, what the row shows
    };

    void Rescan();

    WorldDocument& WorldDoc;
    SelectionService& Selection;
    CommandStack& Commands;
    std::vector<std::filesystem::path> ContentRoots;
    std::vector<Entry> Entries;
    bool Scanned = false;
    char FilterText[64] = {};
};
