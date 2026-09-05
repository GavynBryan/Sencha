#pragma once

#include <ecs/EntityId.h>

class World;

// The FPS composition rule: the body this machine drives is the one whose
// LookOrientation takes this machine's look input. The engine publishes which
// entity that is (LocalControlSubjectOf); this moves the LocalLookControl tag
// to follow it, and is the only thing in this game that adds or removes it.
//
// `tagged` is the body carrying the tag now, held by the caller across frames
// so the move is an edge rather than a scan. Returns true when it moved.
//
// Pure world math, so it is tested without an engine. A pending look input
// accumulated while the tag was on neither body is not lost: it sits in the
// PendingLookInput resource until a tagged body consumes it.
bool FollowLocalLookControl(World& world, EntityId& tagged);
