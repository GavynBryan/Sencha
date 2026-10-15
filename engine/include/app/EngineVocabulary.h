#pragma once

class World;

// The engine's verbs and perception's senses and queries, into a World that
// InstallAuthoredVocabulary gave catalogs. Runtime and editors share this call.
// False means a declaration was refused; the catalogs hold the reasons.
[[nodiscard]] bool InstallEngineVocabulary(World& world);
