#pragma once

#include <render/PresentationSettings.h>

class WorldDocument;

// The settings in force where the editor's focus is, as Play would resolve them
// at that zone: the game's layer, then a world's and the zone's graph's, then the
// focus scene's. A world scene in focus contributes no layer of its own.
[[nodiscard]] PresentationSettings FocusPresentation(const WorldDocument& world, const PresentationSettingsLayer* game);
