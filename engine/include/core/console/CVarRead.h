#pragma once

#include <string>
#include <string_view>

class ConsoleRegistry;

//=============================================================================
// Typed cvar reads with a fallback
//
// `ConsoleRegistry::FindCVar` hands back a `CVarMetadata` whose value is a
// variant, so every caller that wants a number ends up writing the same three
// steps: null-check the registry, null-check the lookup, and pull the
// alternative out without assuming it is there. Written inline that is four
// lines per tunable, and the third step is the one people skip -- a cvar
// registered as an integer read as a double silently yields the fallback
// rather than the configured value.
//
// The registry pointer is nullable on purpose. Most callers hold one only when
// a console exists (headless tools, tests, a game booted without the debug UI),
// and folding that check in here is what keeps the check from being restated at
// every call site.
//
// These are reads. Writing goes through `ConsoleRegistry::SetCVar`, which has
// phase and permission rules these deliberately do not touch.
//=============================================================================

[[nodiscard]] double ReadCVarDouble(const ConsoleRegistry* console,
                                    std::string_view name,
                                    double fallback);

// The render and editor tunables are float-typed almost without exception, so
// this exists to keep the narrowing cast in one place rather than at each site.
[[nodiscard]] float ReadCVarFloat(const ConsoleRegistry* console,
                                  std::string_view name,
                                  float fallback);

[[nodiscard]] bool ReadCVarBool(const ConsoleRegistry* console,
                                std::string_view name,
                                bool fallback);

// Returns a copy: the registry may rewrite the value between calls, so handing
// back a view into its storage would invite a dangling read.
[[nodiscard]] std::string ReadCVarString(const ConsoleRegistry* console,
                                         std::string_view name,
                                         std::string_view fallback);
