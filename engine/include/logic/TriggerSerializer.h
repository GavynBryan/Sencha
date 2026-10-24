#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

// The trigger's scene form: its policy fields, and its sequence as the authored
// {"conditions": [...], "commands": [...]} it was written as. The sequence is
// carried verbatim, so a scene saved by a World that cannot resolve one of its
// verbs or queries keeps what it said.
[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeTriggerSerializer();
