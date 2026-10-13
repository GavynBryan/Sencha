#pragma once

#include "documents/DocumentRef.h"
#include "document/WorldMap.h"
#include "input/InputEvent.h"
#include "input/InputRouter.h"

#include <math/geometry/2d/Rect2d.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

class ActionCatalog;
class ActionContext;
class EditorWorkspace;
class PartitionOutline;

// What fills the Level's content: the views, or the world map over them.
enum class LevelContent : std::uint8_t
{
    Views,
    Map,
};

//=============================================================================
// WorldMapMode
//
// The world map in place of the views: laid out from the world each frame it
// shows, its plates hit-tested by the pointer, and left by going to a zone or
// by Escape. It takes the pointer and Escape from everything under it while
// up. Only a world has one. Whatever draws it reads the map it laid out.
//=============================================================================
class WorldMapMode
{
public:
    static constexpr std::string_view kAction = "view.map";

    WorldMapMode(EditorWorkspace& workspace, PartitionOutline& partition, DocumentRef level);

    void Offer(ActionCatalog& catalog, ActionContext& context);

    [[nodiscard]] LevelContent Content() const { return Showing; }
    // Per frame: the map laid out over `content`; null while it is down or unseen.
    [[nodiscard]] const WorldMap* LayOut(const Rect2d& content, bool visible);
    [[nodiscard]] std::string Title() const;
    // The plate under the pointer.
    [[nodiscard]] std::optional<ZoneId> Hot() const { return Pointed; }
    InputConsumed OnInput(const InputEvent& event);

private:
    // Edits `zone` with the camera on it.
    void Travel(ZoneId zone);

    EditorWorkspace& Workspace;
    PartitionOutline& Partition;
    DocumentRef Level;
    LevelContent Showing = LevelContent::Views;
    WorldMap Map;
    std::optional<ZoneId> Pointed;
};
