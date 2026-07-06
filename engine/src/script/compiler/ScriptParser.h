#pragma once

#include "ScriptAst.h"

#include <string>

struct ScriptParseResult
{
    bool Ok = false;
    std::string Error;
    uint32_t ErrorLine = 0;
    uint32_t ErrorCol = 0;
    ScriptAstUnit Unit;
};

// Parses one lexed source file. `source` must outlive the returned AST
// (string_views alias it).
[[nodiscard]] ScriptParseResult ParseScript(std::string_view path,
                                            const std::vector<ScriptToken>& tokens);
