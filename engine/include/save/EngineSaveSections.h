#pragma once

class Engine;
class SavedGames;

// The sections every save carries: the local participant's body
// ("sencha.bodies") and the runtime persistent id counter ("sencha.persistent_ids").
void RegisterEngineSaveSections(SavedGames& saves, Engine& engine);
