#pragma once

class ComponentRegistrar;

// Every component this game owns, named once, callable from any host that
// needs the game's schema without the game's lifecycle: the module's
// OnRegisterComponents forwards here, and headless cook fixtures register the
// same list so this game's authored content survives a cook outside the editor.
void RegisterHorrorComponents(ComponentRegistrar& registrar);
