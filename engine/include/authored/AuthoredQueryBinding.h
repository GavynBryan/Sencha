#pragma once

#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbBindingCompiler.h>

struct AuthoredQueryBindingDesc
{
    std::string Key;
    std::string QueryName;
    std::vector<std::string> Inputs;
    std::vector<VerbBindingArgument> Arguments;
};

struct CompiledAuthoredQueryBinding
{
    AuthoredQueryHandle Query;
    AuthoredArguments Constants;
    std::vector<VerbCompiledInput> Inputs;
    bool ReferencesChecked = true;
};

[[nodiscard]] bool CompileAuthoredQueryBinding(const AuthoredQueryBindingDesc &desc,
                                               const AuthoredQueryRegistry &queries,
                                               const VerbBindingEnvironment &environment,
                                               CompiledAuthoredQueryBinding &out, std::vector<std::string> &errors);
