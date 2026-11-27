#pragma once

#include <string_view>

class World;

//=============================================================================
// The engine's own authored vocabulary
//
// Names and argument contracts only. Declaring them installs no behaviour: a
// host with no application shell, and an editor with no game running at all,
// both install these so an author can see and validate the vocabulary, and both
// report the operations as unavailable until something binds them.
//
// This file names no service and includes no host type, which is what lets the
// runtime and every editor share one declaration rather than each keeping a
// list that nothing forces to agree.
//=============================================================================

// Every engine domain's verbs, leases and queries, and perception's
// senses and queries, into a World that InstallAuthoredVocabulary gave catalogs,
// as the provider "engine". False means a catalog is missing or a declaration
// was refused; the reasons are on the registries' installation errors.
[[nodiscard]] bool InstallEngineVocabulary(World& world);

// Every kit's registries -- movement, inventory, equipment -- whichever a game installs, for a
// tool World that must read any project's content.
void InstallEveryKitVocabulary(World& world);
