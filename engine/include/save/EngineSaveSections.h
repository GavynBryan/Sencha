#pragma once

class Engine;
class SavedGames;

// The sections every save carries beside the id counter SavedGames writes: the
// local participant's body ("sencha.bodies") and what the world and that body
// hold ("sencha.held").
void RegisterEngineSaveSections(SavedGames& saves, Engine& engine);
