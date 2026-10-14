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
// The world map in place of the views: laid out each frame it shows, hit-tested,
// and left by going to a zone or by Escape. While up it takes the pointer and Escape.
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
