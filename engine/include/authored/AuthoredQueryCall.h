#pragma once

#include <authored/AuthoredQueryId.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/AuthoredValue.h>
#include <core/json/JsonValue.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>


// A query with its constant arguments compiled against its declaration. The
// entity arguments a caller fills per call hold nothing until it does.
struct AuthoredQueryCall
{
    AuthoredQueryHandle Query;
    AuthoredArguments Arguments;
};

// `entityArguments` names the arguments left to the caller; each must be an
// Entity argument, and its slot is written to `entitySlots` in the same order.
// Collects every problem before returning false.
[[nodiscard]] bool CompileAuthoredQueryCall(const AuthoredQueryRegistry& registry,
                                            std::string_view query,
                                            const JsonValue& constants,
                                            std::span<const std::string_view> entityArguments,
                                            std::span<std::size_t> entitySlots,
                                            std::string_view subject,
                                            AuthoredQueryCall& out,
                                            std::vector<std::string>& errors);
