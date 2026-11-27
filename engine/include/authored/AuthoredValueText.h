#pragma once

#include <authored/AuthoredValue.h>

#include <string>

class GameplayTagRegistry;

// A value as a person reads it; a tag by name when `tags` is given.
[[nodiscard]] std::string AuthoredValueText(const AuthoredValue& value, const GameplayTagRegistry* tags);
