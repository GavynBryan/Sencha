#pragma once

class PostEffectCatalog;

// The effects the engine ships: "sencha.fade", a display-domain fade to params[0].rgb.
void RegisterEnginePostEffects(PostEffectCatalog& catalog);
